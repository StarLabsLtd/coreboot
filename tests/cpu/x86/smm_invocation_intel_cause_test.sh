#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include/arch" "$temporary/include/intelblocks" \
	"$temporary/include/soc"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	> "$temporary/include/config.h"

cat > "$temporary/include/arch/io.h" <<'EOF'
#include <stdint.h>
uint8_t inb(uint16_t port);
uint32_t inl(uint16_t port);
void outb(uint8_t value, uint16_t port);
EOF
cat > "$temporary/include/soc/pm.h" <<'EOF'
#define APM_STS_BIT 5
#define ACPI_BASE_ADDRESS 0x1800
#define SMI_STS 0x34
EOF

includes="-I$temporary/include -I$root/src/include -I$root/src \
-I$root/src/soc/intel/common/block/include \
-I$root/src/arch/x86/include -I$root/src/commonlib/include \
-I$root/src/commonlib/bsd/include"
cflags="-std=gnu11 -Wall -Wextra -Werror -Wframe-larger-than=2048 \
-D__COREBOOT__ -D__TEST__ \
-include $root/src/include/kconfig.h -include $root/src/include/rules.h \
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h \
-DCONFIG_PAYLOAD_SPI_FLASH_CONSOLE=0 \
-DCONFIG_CAPSULE_BROKER_ENDPOINT_PUBLICATION=0 \
-DCONFIG_STARLABS_SMM_OPTION_HANDLER=0 \
-DCONFIG_STARLABS_ACPI_EFI_OPTION_SMI=0 \
-DCONFIG_BOARD_ACER_VN7_572G=0 \
-DCONFIG_SMM_APMC_ROUTE_ACPI_CONTROL=0 \
-DCONFIG_SMM_APMC_ROUTE_FINALIZE=0 \
-DCONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE=1 \
-DCONFIG_SMM_APMC_ROUTE_XHCI=0 -DCONFIG_SMM_APMC_ROUTE_LEGACY=0 \
-DCONFIG_SMM_APMC_ROUTE_CFR=0 -DCONFIG_SMM_APMC_ROUTE_OPAL=0 \
-DCONFIG_SMM_APMC_ROUTE_ROM_ARMOR=0 -DCONFIG_SMM_APMC_ROUTE_SMMINFO=0 \
-DCONFIG_SMM_APMC_ROUTE_SMMSTORE=0 -DCONFIG_SMM_APMC_ROUTE_ELOG=0"
sources="$root/tests/cpu/x86/smm_invocation_intel_cause_test.c \
$root/src/soc/intel/common/block/smm/invocation_cause.c \
$root/src/cpu/x86/smm_invocation_loader_composition_gate.c \
$root/src/cpu/x86/smm_invocation_topology.c \
$root/src/cpu/x86/smm_invocation_loader_instance.c"

for optimization in 0 2; do
	${CC:-cc} $cflags -O$optimization $includes $sources \
		-o "$temporary/cause-O$optimization"
	"$temporary/cause-O$optimization"
done

${CC:-cc} $cflags -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined $includes $sources -o "$temporary/cause-san"
ASAN_OPTIONS=detect_leaks=1 "$temporary/cause-san"

${CC:-cc} $cflags -O2 -m32 -march=i686 -ffreestanding -fno-builtin \
	-fno-pie -fstack-usage $includes -c \
	"$root/src/soc/intel/common/block/smm/invocation_cause.c" \
	-o "$temporary/cause-i686.o"
if nm -u "$temporary/cause-i686.o" | grep -q '__atomic_'; then
	printf '%s\n' 'i686 cause object gained a libatomic dependency' >&2
	exit 1
fi
if objdump -d "$temporary/cause-i686.o" | grep -Eq 'call[l]?[[:space:]]+\*'; then
	printf '%s\n' 'cause provider gained an indirect call edge' >&2
	exit 1
fi
frame=$(awk -F '\t' \
	'$1 ~ /intel_smm_invocation_private_cause$/ { print $2 }' \
	"$temporary/cause-i686.su")
test -n "$frame"
test "$frame" -le 1024
test "$(nm --defined-only "$temporary/cause-i686.o" | awk \
	'$3 == "intel_smm_invocation_private_cause" { count++ } END { print count + 0 }')" \
	-eq 1
for source in smm_invocation_loader_composition_gate \
	smm_invocation_topology smm_invocation_loader_instance; do
	${CC:-cc} $cflags -O2 -m32 -march=i686 -ffreestanding -fno-builtin \
		-fno-pie -fstack-usage $includes -c "$root/src/cpu/x86/$source.c" \
		-o "$temporary/$source.o"
done
gate_frame=$(awk -F '\t' \
	'$1 ~ /smm_invocation_loader_composition_evidence$/ { print $2 }' \
	"$temporary/smm_invocation_loader_composition_gate.su")
topology_frame=$(awk -F '\t' \
	'$1 ~ /smm_invocation_topology_read$/ { print $2 }' \
	"$temporary/smm_invocation_topology.su")
instance_frame=$(awk -F '\t' \
	'$1 ~ /smm_invocation_loader_instance_read$/ { print $2 }' \
	"$temporary/smm_invocation_loader_instance.su")
test -n "$gate_frame" && test -n "$topology_frame" && \
	test -n "$instance_frame"
largest_callee=$topology_frame
if [ "$instance_frame" -gt "$largest_callee" ]; then
	largest_callee=$instance_frame
fi
if [ "$gate_frame" -gt "$largest_callee" ]; then
	largest_callee=$gate_frame
fi
test "$((frame + largest_callee))" -le 1536

production="$root/src/soc/intel/common/block/smm/invocation_cause.c"
mutant_source="$temporary/invocation_cause.mutant.c"

kill_mutant()
{
	name=$1
	if cmp -s "$production" "$mutant_source"; then
		printf 'mutant did not change source: %s\n' "$name" >&2
		exit 1
	fi
	if ! ${CC:-cc} $cflags -O2 $includes \
		"$root/tests/cpu/x86/smm_invocation_intel_cause_test.c" \
		"$mutant_source" \
		"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
		"$root/src/cpu/x86/smm_invocation_topology.c" \
		"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
		-o "$temporary/$name-mutant"; then
		printf 'mutant failed to compile: %s\n' "$name" >&2
		exit 1
	fi
	if "$temporary/$name-mutant" >/dev/null 2>&1; then
		printf 'surviving mutant: %s\n' "$name" >&2
		exit 1
	fi
}

sed 's/sample_a.smi_status != (1U << APM_STS_BIT)/!(sample_a.smi_status \& (1U << APM_STS_BIT))/' \
	"$production" > "$mutant_source"
kill_mutant exclusive-status

sed 's/if (sample_a.command != SMM_APMC_AUTHVAR_PRESENCE)/if (0)/' \
	"$production" > "$mutant_source"
kill_mutant sample-a-command

sed 's/sample_a.smi_status != sample_b.smi_status/((sample_a.smi_status ^ sample_b.smi_status) \& 0U)/' \
	"$production" > "$mutant_source"
kill_mutant sample-b-status

sed 's/sample_a.command != sample_b.command/((sample_a.command ^ sample_b.command) \& 0U)/' \
	"$production" > "$mutant_source"
kill_mutant sample-b-command

sed 's/sample_a.smi_status != (1U << APM_STS_BIT)/(sample_a.smi_status \& ~(1U << 31)) != (1U << APM_STS_BIT)/' \
	"$production" > "$mutant_source"
kill_mutant selective-status-mask

sed '/evidence_a = smm_invocation_loader_composition_evidence/{N;s/evidence_a = smm_invocation_loader_composition_evidence(\n[[:space:]]*composition, evidence);/evidence_a = evidence;/;}' \
	"$production" > "$mutant_source"
kill_mutant composition-gate-a

sed '/evidence_b = smm_invocation_loader_composition_evidence/{N;s/evidence_b = smm_invocation_loader_composition_evidence(\n[[:space:]]*composition, evidence);/evidence_b = evidence_a;/;}' \
	"$production" > "$mutant_source"
kill_mutant composition-gate-b

sed '/static bool evidence_ready/,/^}/s/return __atomic_load_n.*/return (void)evidence, true; \/\*/; /static bool evidence_ready/,/^}/s/^[[:space:]]*SMM_INVOCATION_READY;/ * removed READY check *\//' \
	"$production" > "$mutant_source"
kill_mutant evidence-ready

sed 's/evidence->loader_instance_nonce)/instance->loader_instance_nonce)/' \
	"$production" > "$mutant_source"
kill_mutant evidence-binding

sed 's/!memcmp(\&topology_a, \&topology_b,/!memcmp(\&topology_a, \&topology_a,/' \
	"$production" > "$mutant_source"
kill_mutant topology-equality

sed 's/!memcmp(\&instance_a, \&instance_b,/!memcmp(\&instance_a, \&instance_a,/' \
	"$production" > "$mutant_source"
kill_mutant instance-equality

sed -e 's/runtime_cpus == topology->active_cpus/true/' \
	-e 's/runtime_a == runtime_b/true/' "$production" > "$mutant_source"
kill_mutant runtime-equality

sed 's/const bool output_safe = valid_inputs \&\& output_disjoint(/const bool output_safe = valid_inputs || output_disjoint(/' \
	"$production" > "$mutant_source"
kill_mutant output-alias

sed 's/memcpy(cause, \&cause_value, sizeof(\*cause))/memset(cause, 0, sizeof(*cause))/' \
	"$production" > "$mutant_source"
kill_mutant publication

printf '%s\n' 'SMM invocation Intel private-cause tests: PASS'
