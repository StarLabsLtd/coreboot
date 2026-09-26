#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

profile()
{
	name=$1
	shift
	mkdir -p "$temporary/$name/include"
	{
		printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0'
		printf '%s\n' '#define CONFIG_MAX_CPUS 4'
		printf '%s\n' '#define CONFIG_SMM_APMC_COMMAND_REGISTRY 1'
		printf '%s\n' '#define CONFIG_SMM_APMC_COMPOSITION_ATTESTED 1'
		for symbol in "$@"; do
			printf '#define CONFIG_%s 1\n' "$symbol"
		done
	} > "$temporary/$name/include/config.h"
}

profile baseline SMM_APMC_ROUTE_ACPI_CONTROL SMM_APMC_ROUTE_FINALIZE
profile spi-q35 PAYLOAD_SPI_FLASH_CONSOLE BOARD_EMULATION_QEMU_X86_Q35 \
	SMM_APMC_ROUTE_ACPI_CONTROL SMM_APMC_ROUTE_FINALIZE \
	SMM_APMC_ROUTE_SPI_CONSOLE
profile spi-intel PAYLOAD_SPI_FLASH_CONSOLE
profile capsule CAPSULE_BROKER_ENDPOINT_PUBLICATION
profile starlabs STARLABS_SMM_OPTION_HANDLER STARLABS_ACPI_EFI_OPTION_SMI \
	SMM_APMC_ROUTE_STARLABS_EFI_OPTION SMM_APMC_ROUTE_ACPI_CONTROL \
	SMM_APMC_ROUTE_FINALIZE DRIVERS_OPTION_CFR_RUNTIME_APPLY \
	TCG_OPAL_S3_UNLOCK SMMSTORE ELOG_GSMI SMM_APMC_ROUTE_CFR \
	SMM_APMC_ROUTE_OPAL SMM_APMC_ROUTE_SMMSTORE SMM_APMC_ROUTE_ELOG
profile acer BOARD_ACER_VN7_572G SMM_APMC_ROUTE_ACER_BOARD \
	SMM_APMC_ROUTE_ACPI_CONTROL
profile services DRIVERS_OPTION_CFR_RUNTIME_APPLY TCG_OPAL_S3_UNLOCK \
	SMMSTORE ELOG_GSMI SMM_APMC_ROUTE_ACPI_CONTROL \
	SMM_APMC_ROUTE_FINALIZE SMM_APMC_ROUTE_CFR SMM_APMC_ROUTE_OPAL \
	SMM_APMC_ROUTE_SMMSTORE SMM_APMC_ROUTE_ELOG
profile amd-services SOC_AMD_COMMON_BLOCK_PSP_ROM_ARMOR3 \
	SMM_APMC_ROUTE_ACPI_CONTROL SMM_APMC_ROUTE_ROM_ARMOR \
	SMM_APMC_ROUTE_SMMINFO
profile legacy-xhci SMM_APMC_ROUTE_ACPI_CONTROL \
	SMM_APMC_ROUTE_LEGACY SMM_APMC_ROUTE_XHCI
profile e8-collision PAYLOAD_SPI_FLASH_CONSOLE BOARD_EMULATION_QEMU_X86_Q35 \
	CAPSULE_BROKER_ENDPOINT_PUBLICATION SMM_APMC_ROUTE_SPI_CONSOLE \
	SMM_APMC_ROUTE_CAPSULE_BROKER
mkdir -p "$temporary/unsupported/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 4' \
	'#define CONFIG_SMM_APMC_COMMAND_REGISTRY 1' \
	> "$temporary/unsupported/include/config.h"

resolve_config()
{
	name=$1
	source=$2
	resolved="$temporary/$name.config"
	cp "$root/$source" "$resolved"
	make -C "$root" KCONFIG_CONFIG="$resolved" olddefconfig >/dev/null
}

# Exercise actual Kconfig selection rather than only synthetic config.h files.
resolve_config resolved-q35 configs/config.emulation_qemu_x86_q35_smm_tseg
grep -q '^CONFIG_SMM_APMC_COMPOSITION_ATTESTED=y$' \
	"$temporary/resolved-q35.config"
for route in ACPI_CONTROL ELOG FINALIZE SMMSTORE SPI_CONSOLE; do
	grep -q "^CONFIG_SMM_APMC_ROUTE_$route=y$" \
		"$temporary/resolved-q35.config"
done
resolve_config resolved-mtl configs/config.starlabs_starbook_mtl
grep -q '^CONFIG_SMM_APMC_COMPOSITION_ATTESTED=y$' \
	"$temporary/resolved-mtl.config"
grep -q '^CONFIG_SMM_APMC_ROUTE_STARLABS_EFI_OPTION=y$' \
	"$temporary/resolved-mtl.config"
for unsupported_config in \
	configs/config.emulation_qemu_x86_i440fx \
	configs/config.starlabs_starbook_cezanne; do
	name=$(basename "$unsupported_config")
	resolve_config "$name" "$unsupported_config"
	if grep -q '^CONFIG_SMM_APMC_COMPOSITION_ATTESTED=y$' \
		"$temporary/$name.config"; then
		printf 'unsupported Kconfig composition attested: %s\n' \
			"$unsupported_config" >&2
		exit 1
	fi
done

build()
{
	profile_name=$1
	name=$2
	flags=$3
	source=${4:-$root/src/cpu/x86/smm_command.c}
	header=${5:-$root/src/include}
	# Deliberate normal flag splitting for this host-only strict harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-fno-builtin $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/$profile_name/include" -I"$header" -I"$root/src" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_command_test.c" "$source" \
		-o "$temporary/$name"
}

run_profile()
{
	profile_name=$1
	optimization=$2
	flags="-O$optimization"
	name="$profile_name-O$optimization"

	build "$profile_name" "$name" "$flags"
	"$temporary/$name"
	name="$profile_name-sanitize-O$optimization"
	build "$profile_name" "$name" \
		"$flags -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/$name"
}

for profile_name in baseline spi-q35 starlabs acer services amd-services \
	legacy-xhci; do
	run_profile "$profile_name" 0
	run_profile "$profile_name" 2
done

unsupported_log="$temporary/unsupported.log"
if build unsupported unattested-build -O2 >"$unsupported_log" 2>&1; then
	printf '%s\n' 'unsupported APMC composition compiled' >&2
	exit 1
fi
grep -q 'APMC registry requires an attested dispatcher composition' \
	"$unsupported_log"

# Every symbolic command used by a case, trigger, or table is reserved. The
# board-local Acer name is pinned below; APM_CNT_COMMAND is CFR documentation.
rg -o --no-filename 'APM_CNT_[A-Z0-9_]+' "$root/src" --glob '*.[ch]' | \
	sort -u | while read -r command; do
	case "$command" in
	APM_CNT_BOARD_SMI|APM_CNT_COMMAND)
		continue
		;;
	esac
	if ! grep -q "ENTRY($command)" \
		"$root/src/include/cpu/x86/smm_command.h"; then
		printf 'unreserved symbolic APMC use: %s\n' "$command" >&2
		exit 1
	fi
done

# Mainboard hooks are explicit audit inputs, not silently treated as owners or
# observers. A new hook forces this inventory to be reviewed.
rg -l '^int mainboard_smi_apmc' "$root/src" | \
	sed "s#^$root/##" | sort > "$temporary/mainboard-hooks.txt"
diff -u "$root/tests/cpu/x86/smm_apmc_mainboard_hooks.txt" \
	"$temporary/mainboard-hooks.txt"


# An enabled owner without exactly one dispatcher binding is a build error.
for orphan in spi-intel capsule; do
	orphan_log="$temporary/$orphan-orphan.log"
	if build "$orphan" "$orphan-orphan" -O2 >"$orphan_log" 2>&1; then
		printf 'enabled owner without dispatcher compiled: %s\n' "$orphan" >&2
		exit 1
	fi
	grep -q 'enabled APMC owner needs one dispatcher' "$orphan_log"
done

# The real capsule/SPI e8 alias must fail at compile time when both are enabled.
collision_log="$temporary/e8-collision.log"
if build e8-collision e8-collision -O2 >"$collision_log" 2>&1; then
	printf '%s\n' 'capsule/SPI e8 collision compiled' >&2
	exit 1
fi
grep -Eq 'duplicate case value|duplicate case' "$collision_log"

mutation()
{
	name=$1
	profile_name=$2
	expression=$3
	mutant="$temporary/$name.c"

	sed "$expression" "$root/src/cpu/x86/smm_command.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/cpu/x86/smm_command.c"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$name-O$optimization"
		if ! build "$profile_name" "$binary" \
			"-O$optimization -fsanitize=address,undefined -fno-sanitize-recover=all" \
			"$mutant" >/dev/null 2>&1; then
			printf 'mutation did not compile: %s O%s\n' "$name" \
				"$optimization" >&2
			exit 1
		fi
		if "$temporary/$binary" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

mutation reserved-falls-through baseline \
	's/!enabled && !command_reserved(command)/!enabled \&\& false \&\& !command_reserved(command)/'
mutation accept-owner-error baseline \
	's/outcome != SMM_APMC_OWNER_HANDLED/(outcome != SMM_APMC_OWNER_HANDLED \&\& false)/'

binding_mutant="$temporary/ignore-binding.c"
sed 's/(bindings) == 1/(bindings) >= 0/' \
	"$root/src/cpu/x86/smm_command.c" > "$binding_mutant"
if ! build spi-intel ignore-binding -O2 "$binding_mutant" >/dev/null 2>&1; then
	printf '%s\n' 'binding-count mutant did not remove the build guard' >&2
	exit 1
fi

# The reserved-value manifest itself must reject an alias.
mkdir -p "$temporary/duplicate/cpu/x86"
sed 's/ENTRY(APM_CNT_ELOG_GSMI)/ENTRY(APM_CNT_SMMSTORE)/' \
	"$root/src/include/cpu/x86/smm_command.h" \
	> "$temporary/duplicate/cpu/x86/smm_command.h"
if build baseline duplicate-reservation -O2 \
	"$root/src/cpu/x86/smm_command.c" "$temporary/duplicate" \
	>/dev/null 2>&1; then
	printf '%s\n' 'duplicate reserved APMC value compiled' >&2
	exit 1
fi

# Every shared APM_CNT definition has exactly one namespace reservation.
sed -n 's/^#define[[:space:]]\+\(APM_CNT_[A-Z0-9_]*\).*/\1/p' \
	"$root/src/include/cpu/x86/smm.h" | while read -r command; do
	count=$(grep -c "ENTRY($command)" \
		"$root/src/include/cpu/x86/smm_command.h")
	if [ "$count" -ne 1 ]; then
		printf 'APMC definition has %s reservations: %s\n' "$count" "$command" >&2
		exit 1
	fi
done

# Platform-local constants are pinned so source drift cannot silently escape.
grep -Eq '^#define CAPSULE_BROKER_APM_COMMAND[[:space:]]+0xe8U$' \
	"$root/src/include/boot/capsule_broker.h"
grep -Eq '^#define PAYLOAD_SPI_CONSOLE_APM_CMD[[:space:]]+0xe8U$' \
	"$root/src/include/console/payload_spi_console.h"
grep -Eq '^#define STARLABS_APMC_CMD_EFI_OPTION[[:space:]]+0x[Ee]2$' \
	"$root/src/mainboard/starlabs/common/include/starlabs/efi_option_smi.h"
grep -Eq '^#define APM_CNT_BOARD_SMI[[:space:]]+0x[Dd][Dd]$' \
	"$root/src/mainboard/acer/aspire_vn7_572g/smihandler.c"
grep -q 'case PAYLOAD_SPI_CONSOLE_APM_CMD:' \
	"$root/src/mainboard/emulation/qemu-q35/smihandler.c"
for command in APM_CNT_ACPI_DISABLE APM_CNT_ACPI_ENABLE APM_CNT_FINALIZE \
	APM_CNT_ELOG_GSMI APM_CNT_SMMSTORE; do
	grep -q "case $command:" \
		"$root/src/mainboard/emulation/qemu-q35/smihandler.c"
done
if rg -q 'payload_spi_console_smi' \
	"$root/src/soc/intel/common/block/smm/smihandler.c"; then
	printf '%s\n' 'Intel common SPI-console orphan audit became stale' >&2
	exit 1
fi
for command in APM_CNT_ACPI_DISABLE APM_CNT_ACPI_ENABLE \
	APM_CNT_CFR_RUNTIME_APPLY APM_CNT_ELOG_GSMI APM_CNT_FINALIZE \
	APM_CNT_SMMSTORE; do
	grep -q "case $command:" \
		"$root/src/soc/intel/common/block/smm/smihandler.c"
done
grep -q 'opal_s3_smi_apmc(reg8)' \
	"$root/src/soc/intel/common/block/smm/smihandler.c"

# Q35 is the terminal dispatcher. The attested StarLabs hook owns only e2 and
# must not become a lifecycle observer without an explicit observer manifest.
q35_apmc_count=$(grep -c 'mainboard_smi_apmc(' \
	"$root/src/mainboard/emulation/qemu-q35/smihandler.c" || true)
if [ "$q35_apmc_count" -ne 0 ]; then
	printf '%s\n' 'Q35 gained a downstream mainboard APMC hook' >&2
	exit 1
fi
sed -n '/^int mainboard_smi_apmc/,/^}/p' \
	"$root/src/mainboard/starlabs/common/smihandler.c" \
	> "$temporary/starlabs-apmc-hook.c"
awk '/^int mainboard_smi_apmc/ { copy = 1 } copy { print; if (++lines == 4) exit }' \
	"$root/src/mainboard/starlabs/common/smihandler.c" \
	> "$temporary/starlabs-apmc-prefix.c"
cat > "$temporary/starlabs-apmc-prefix.expected" <<'EOF'
int mainboard_smi_apmc(u8 data)
{
	if (data != STARLABS_APMC_CMD_EFI_OPTION)
		return 0;
EOF
diff -u "$temporary/starlabs-apmc-prefix.expected" \
	"$temporary/starlabs-apmc-prefix.c"
if rg -q 'APM_CNT_ACPI_(ENABLE|DISABLE)|APM_CNT_FINALIZE' \
	"$temporary/starlabs-apmc-hook.c"; then
	printf '%s\n' 'StarLabs hook gained an unmanifested lifecycle observer' >&2
	exit 1
fi

# Route capabilities must be selected by the Kconfig family containing the
# audited dispatch case, never by the service being claimed.
grep -q 'select SMM_APMC_ROUTE_SPI_CONSOLE' \
	"$root/src/mainboard/emulation/qemu-q35/Kconfig"
grep -q 'select SMM_APMC_ROUTE_STARLABS_EFI_OPTION' \
	"$root/src/mainboard/starlabs/common/Kconfig"
grep -q 'select SMM_APMC_ROUTE_ACER_BOARD' \
	"$root/src/mainboard/acer/aspire_vn7_572g/Kconfig"
for route in ACPI_CONTROL CFR ELOG FINALIZE OPAL SMMSTORE; do
	grep -q "select SMM_APMC_ROUTE_$route" \
		"$root/src/soc/intel/common/block/smm/Kconfig"
done
grep -q 'select SMM_APMC_COMPOSITION_ATTESTED' \
	"$root/src/mainboard/emulation/qemu-q35/Kconfig"
grep -q 'select SMM_APMC_COMPOSITION_ATTESTED' \
	"$root/src/mainboard/starlabs/common/Kconfig"
if rg -q 'select SMM_APMC_COMPOSITION_ATTESTED' "$root/src" \
	-g '!src/mainboard/emulation/qemu-q35/Kconfig' \
	-g '!src/mainboard/starlabs/common/Kconfig'; then
	printf '%s\n' 'unsupported APMC composition became attested' >&2
	exit 1
fi
if rg -q '^[[:space:]]*select[[:space:]]+SMM_APMC_' \
	"$root/src/mainboard" -g 'Kconfig.name'; then
	printf '%s\n' 'APMC route select added to forbidden Kconfig.name' >&2
	exit 1
fi

grep -qx 'smm-$(CONFIG_SMM_APMC_COMMAND_REGISTRY) += smm_command.c' \
	"$root/src/cpu/x86/Makefile.mk"
if rg -q 'select[[:space:]]+SMM_APMC_COMMAND_REGISTRY' "$root/src"; then
	printf '%s\n' 'APMC command registry became selected' >&2
	exit 1
fi
grep -q 'depends on HAVE_SMI_HANDLER && SMM_APMC_COMPOSITION_ATTESTED' \
	"$root/src/cpu/x86/Kconfig"
if rg -q 'smm_apmc_command_classify' "$root/src" \
	-g '!src/cpu/x86/smm_command.c' \
	-g '!src/include/cpu/x86/smm_command.h'; then
	printf '%s\n' 'dormant classifier gained a production callsite' >&2
	exit 1
fi

# Check the complete tracked and untracked patch. X-macro diagnostics are
# normalized to exact type/path/line/message/anchor rows; no class is ignored.
changed_files="$temporary/changed-files.txt"
{
	git -C "$root" diff --name-only HEAD
	git -C "$root" ls-files --others --exclude-standard
} | sort -u > "$changed_files"
cat > "$temporary/expected-files.txt" <<'EOF'
Documentation/arch/x86/smm-apmc-command-registry.md
src/cpu/x86/Kconfig
src/cpu/x86/Makefile.mk
src/cpu/x86/smm_command.c
src/include/cpu/x86/smm_command.h
src/mainboard/acer/aspire_vn7_572g/Kconfig
src/mainboard/emulation/qemu-q35/Kconfig
src/mainboard/starlabs/common/Kconfig
src/soc/intel/common/block/smm/Kconfig
tests/cpu/x86/smm_apmc_mainboard_hooks.txt
tests/cpu/x86/smm_command_checkpatch.awk
tests/cpu/x86/smm_command_checkpatch.expected
tests/cpu/x86/smm_command_test.c
tests/cpu/x86/smm_command_test.sh
util/testing/Makefile.mk
EOF
diff -u "$temporary/expected-files.txt" "$changed_files"
checkpatch_patch="$temporary/checkpatch.patch"
{
	git -C "$root" diff --binary HEAD --
	git -C "$root" ls-files --others --exclude-standard | sort | \
	while read -r file; do
		git -C "$root" diff --no-index -- /dev/null "$file" || true
	done
} > "$checkpatch_patch"
checkpatch_output="$temporary/checkpatch.out"
"$root/util/lint/checkpatch.pl" --no-tree --show-types --strict \
	"$checkpatch_patch" > "$checkpatch_output" 2>&1 || true
checkpatch_cache="$root/.checkpatch-camelcase.git."
if [ -e "$checkpatch_cache" ]; then
	[ ! -s "$checkpatch_cache" ]
	unlink "$checkpatch_cache"
fi
awk -f "$root/tests/cpu/x86/smm_command_checkpatch.awk" \
	"$checkpatch_output" > "$temporary/checkpatch.unsorted"
sort "$temporary/checkpatch.unsorted" > "$temporary/checkpatch.actual"
expected_diagnostics="$root/tests/cpu/x86/smm_command_checkpatch.expected"
expected_rows="$temporary/checkpatch.expected.rows"
[ "$(sed -n '1p' "$expected_diagnostics")" = \
	'# SPDX-License-Identifier: GPL-2.0-only' ]
[ "$(grep -cx '# SPDX-License-Identifier: GPL-2.0-only' \
	"$expected_diagnostics")" -eq 1 ]
tail -n +2 "$expected_diagnostics" > "$expected_rows"
for severity in ERROR WARNING CHECK; do
	raw_count=$(grep -c "^$severity:" "$checkpatch_output" || true)
	normalized_count=$(grep -c "^$severity|" \
		"$temporary/checkpatch.actual" || true)
	[ "$raw_count" -eq "$normalized_count" ]
done
raw_count=$(grep -Ec '^(ERROR|WARNING|CHECK):' "$checkpatch_output" || true)
[ "$raw_count" -eq "$(wc -l < "$temporary/checkpatch.actual")" ]
summary_counts=$(sed -nE \
	's/^total: ([0-9]+) errors, ([0-9]+) warnings, ([0-9]+) checks, [0-9]+ lines checked$/\1 \2 \3/p' \
	"$checkpatch_output")
[ "$(printf '%s\n' "$summary_counts" | wc -l)" -eq 1 ]
set -- $summary_counts
[ "$#" -eq 3 ]
[ "$1" -eq "$(grep -c '^ERROR|' "$temporary/checkpatch.actual" || true)" ]
[ "$2" -eq "$(grep -c '^WARNING|' "$temporary/checkpatch.actual" || true)" ]
[ "$3" -eq "$(grep -c '^CHECK|' "$temporary/checkpatch.actual" || true)" ]
[ "$(( $1 + $2 + $3 ))" -eq "$raw_count" ]
[ "$(wc -l < "$expected_rows")" -eq 35 ]
[ "$(sha256sum "$expected_rows" | cut -d' ' -f1)" = \
	d278fb98568ca36c0ca99293c045a376ffc5cfc73623863576730f5049143375 ]
diff -u "$expected_rows" "$temporary/checkpatch.actual"

printf '%s\n' 'SMM APMC command registry tests: PASS'
