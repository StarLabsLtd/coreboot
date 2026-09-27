#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
base=fce222a9cf490cc214e87c8e6ca1dbcfb94d8cb2
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	> "$temporary/include/config.h"

sources="$root/src/cpu/x86/smm_invocation_loader_composition.c
$root/src/cpu/x86/smm_invocation_loader_composition_gate.c
$root/src/cpu/x86/smm_invocation_topology.c
$root/src/cpu/x86/smm_invocation_loader_instance.c
$root/src/cpu/x86/smm_invocation_evidence_loader.c"

build()
{
	output=$1
	flags=$2
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_loader_composition_test.c" \
		$sources -o "$temporary/$output"
}

build_variant()
{
	output=$1
	flags=$2
	composition=$3
	evidence_loader=$4
	gate=$5
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_invocation_loader_composition_test.c" \
		"$composition" "$root/src/cpu/x86/smm_invocation_topology.c" \
		"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
		"$evidence_loader" "$gate" -o "$temporary/$output"
}

mutant()
{
	name=$1
	component=$2
	expression=$3
	case "$component" in
	composition)
		source="$root/src/cpu/x86/smm_invocation_loader_composition.c"
		composition="$temporary/$name.c"
		evidence_loader="$root/src/cpu/x86/smm_invocation_evidence_loader.c"
		gate="$root/src/cpu/x86/smm_invocation_loader_composition_gate.c"
		;;
	evidence)
		source="$root/src/cpu/x86/smm_invocation_evidence_loader.c"
		composition="$root/src/cpu/x86/smm_invocation_loader_composition.c"
		evidence_loader="$temporary/$name.c"
		gate="$root/src/cpu/x86/smm_invocation_loader_composition_gate.c"
		;;
	gate)
		source="$root/src/cpu/x86/smm_invocation_loader_composition_gate.c"
		composition="$root/src/cpu/x86/smm_invocation_loader_composition.c"
		evidence_loader="$root/src/cpu/x86/smm_invocation_evidence_loader.c"
		gate="$temporary/$name.c"
		;;
	esac
	sed "$expression" "$source" > "$temporary/$name.c"
	if cmp -s "$source" "$temporary/$name.c"; then
		printf 'mutant did not change source: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$name-O$optimization"
		build_log="$temporary/$binary.build.log"
		if ! build_variant "$binary" "-O$optimization" "$composition" \
			"$evidence_loader" "$gate" >"$build_log" 2>&1; then
			cat "$build_log" >&2
			printf 'composition mutant did not compile: %s/O%s\n' \
				"$name" "$optimization" >&2
			exit 1
		fi
		if "$temporary/$binary" >/dev/null 2>&1; then
			printf 'composition mutant survived: %s/O%s\n' \
				"$name" "$optimization" >&2
			exit 1
		fi
	done
}

for optimization in 0 2; do
	build "plain-O$optimization" "-O$optimization"
	"$temporary/plain-O$optimization"
	build "sanitize-O$optimization" \
		"-O$optimization -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/sanitize-O$optimization"
done

build tsan "-O1 -g -fsanitize=thread"
TSAN_OPTIONS=halt_on_error=1 "$temporary/tsan"

mutant topology-final-read composition \
	'/COMPOSITION_TEST_HOOK(7)/,/evidence_matches_seed/{s/smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS/false/}'
mutant topology-initial-read composition \
	'/COMPOSITION_TEST_HOOK(1)/,/COMPOSITION_TEST_HOOK(2)/{s/smm_invocation_topology_read(topology, &topology_snapshot) !=/CB_SUCCESS !=/}'
mutant topology-first-recheck composition \
	'/COMPOSITION_TEST_HOOK(4)/,/COMPOSITION_TEST_HOOK(5)/{s/smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS/false/; s/memcmp(&topology_snapshot, &topology_check,/memcmp(\&topology_snapshot, \&topology_snapshot,/;}'
mutant topology-second-recheck composition \
	'/COMPOSITION_TEST_HOOK(6)/,/COMPOSITION_TEST_HOOK(7)/{s/smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS/false/; s/memcmp(&topology_snapshot, &topology_check,/memcmp(\&topology_snapshot, \&topology_snapshot,/;}'
mutant instance-final-read composition \
	'/COMPOSITION_TEST_HOOK(7)/,/evidence_matches_seed/{s/smm_invocation_loader_instance_read(instance, &instance_check) !=/false !=/}'
mutant instance-first-read composition \
	'/COMPOSITION_TEST_HOOK(4)/,/COMPOSITION_TEST_HOOK(5)/{s/smm_invocation_loader_instance_read(instance, &instance_snapshot) !=/CB_SUCCESS !=/}'
mutant instance-second-recheck composition \
	'/COMPOSITION_TEST_HOOK(6)/,/COMPOSITION_TEST_HOOK(7)/{s/smm_invocation_loader_instance_read(instance, &instance_check) !=/CB_SUCCESS !=/; s/memcmp(&instance_snapshot, &instance_check,/memcmp(\&instance_snapshot, \&instance_snapshot,/;}'
mutant evidence-final-read composition \
	's/!evidence_matches_seed(evidence, &evidence_seed)/(evidence_matches_seed(evidence, \&evidence_seed) \&\& false)/'
mutant owner-recheck composition 's/composition->owner_attempt == owner_attempt/true/'
mutant final-ready-store composition \
	'0,/if (!__atomic_compare_exchange_n(&composition->state, &expected,/{s//if ((__atomic_store_n(\&composition->state, SMM_INVOCATION_LOADER_COMPOSITION_READY, __ATOMIC_RELEASE), false) \&\& !__atomic_compare_exchange_n(\&composition->state, \&expected,/}'
mutant provider-take composition \
	's/smm_invocation_platform_loader_instance_take(&instance_seed) !=/CB_SUCCESS !=/'
mutant evidence-unwind composition \
	's/if (evidence_receipt.acquired) {/if (evidence_receipt.acquired \&\& false) {/'
mutant instance-unwind composition \
	's/if (instance_published) {/if (instance_published \&\& false) {/'
mutant topology-unwind composition \
	's/smm_invocation_topology_scrub(topology);/(void)topology;/'
mutant unwind-order composition 's/COMPOSITION_TEST_HOOK(100)/COMPOSITION_TEST_HOOK(102)/'
mutant receipt-terminal-cas evidence \
	'/enum cb_err smm_invocation_evidence_loader_rollback/,/^}/{s/if (!terminal_claim(evidence, expected, SMM_INVOCATION_EMPTY))/if (((void)expected, false))/}'
mutant receipt-identity evidence \
	'/enum cb_err smm_invocation_evidence_loader_rollback/,/^}/{s/receipt->evidence_identity != (uint64_t)(uintptr_t)evidence/false/}'
mutant receipt-acquired evidence \
	'/enum cb_err smm_invocation_evidence_loader_rollback/,/^}/{s/receipt->acquired != 1U/false/}'
mutant finish-failure-retry evidence \
	's/attempt < 2U/attempt < 1U/'
mutant shutdown-first-close evidence \
	'/EVIDENCE_LOADER_TEST_HOOK(36)/,/EVIDENCE_LOADER_TEST_HOOK(37)/{s/if (close_if_shutdown/if (false \&\& close_if_shutdown/}'
mutant shutdown-second-close evidence \
	'/EVIDENCE_LOADER_TEST_HOOK(39)/,/EVIDENCE_LOADER_TEST_HOOK(38)/{s/if (close_if_shutdown/if (false \&\& close_if_shutdown/}'
mutant shutdown-third-close evidence \
	'/EVIDENCE_LOADER_TEST_HOOK(44)/,/EVIDENCE_LOADER_TEST_HOOK(46)/{s/if (close_if_shutdown/if (false \&\& close_if_shutdown/}'
mutant shutdown-gap-one evidence 's/EVIDENCE_LOADER_TEST_HOOK(37);/(void)0;/'
mutant shutdown-gap-two evidence 's/EVIDENCE_LOADER_TEST_HOOK(38);/(void)0;/'
mutant shutdown-gap-three evidence 's/EVIDENCE_LOADER_TEST_HOOK(46);/(void)0;/'
mutant accessor-ready gate \
	'0,/SMM_INVOCATION_LOADER_COMPOSITION_READY/{s/!=/==/}'
mutant accessor-identity gate \
	's/composition->evidence_identity != (uint64_t)(uintptr_t)evidence/false/'

objects=
for source in $sources; do
	object="$temporary/$(basename "$source" .c)-32.o"
	${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
		-Wconversion -Wshadow -ffreestanding -fno-builtin -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -c "$source" -o "$object"
	if nm -u "$object" | grep -q '__atomic_'; then
		printf '%s gained a 32-bit libatomic dependency\n' "$source" >&2
		exit 1
	fi
	objects="$objects $object"
done
ld -m elf_i386 -r $objects -o "$temporary/composition-32-linked.o"

printf '%s\n' \
	'#include <cpu/x86/smm_invocation_loader_composition.h>' \
	'int main(void)' \
	'{' \
	'  struct smm_invocation_loader_instance_seed seed = { 0 };' \
	'  return smm_invocation_platform_loader_instance_take(&seed);' \
	'}' > "$temporary/provider-consumer.c"
printf '%s\n' \
	'#include <cpu/x86/smm_invocation_loader_composition.h>' \
	'enum cb_err smm_invocation_platform_loader_instance_take(' \
	'  struct smm_invocation_loader_instance_seed *seed)' \
	'{' \
	'  (void)seed;' \
	'  return CB_ERR;' \
	'}' > "$temporary/provider.c"
link_flags="-std=gnu11 -D__COREBOOT__ -include $root/src/include/kconfig.h \
-include $root/src/include/rules.h \
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h \
-I$temporary/include -I$root/src/include -I$root/src \
-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include \
-I$root/src/arch/x86/include"
# Deliberate normal flag splitting for this strict host harness.
# shellcheck disable=SC2086
${CC:-cc} $link_flags -c "$temporary/provider-consumer.c" \
	-o "$temporary/provider-consumer.o"
# shellcheck disable=SC2086
${CC:-cc} $link_flags -c "$temporary/provider.c" -o "$temporary/provider.o"
if ${CC:-cc} "$temporary/provider-consumer.o" -o "$temporary/missing" \
	>"$temporary/missing.log" 2>&1; then
	printf '%s\n' 'composition linked without a nonce provider' >&2
	exit 1
fi
grep -q 'smm_invocation_platform_loader_instance_take' \
	"$temporary/missing.log"
${CC:-cc} "$temporary/provider-consumer.o" "$temporary/provider.o" \
	-o "$temporary/one-provider"
if ${CC:-cc} "$temporary/provider-consumer.o" "$temporary/provider.o" \
	"$temporary/provider.o" -o "$temporary/duplicate" \
	>"$temporary/duplicate.log" 2>&1; then
	printf '%s\n' 'composition linked with duplicate nonce providers' >&2
	exit 1
fi
grep -q 'multiple definition.*smm_invocation_platform_loader_instance_take' \
	"$temporary/duplicate.log"

if rg -q '__attribute__[[:space:]]*\(\(weak\)\)|__weak|APM_CNT|outb|lb_new_record|lb_add' \
	"$root/src/cpu/x86/smm_invocation_loader_composition.c" \
	"$root/src/include/cpu/x86/smm_invocation_loader_composition.h"; then
	printf '%s\n' 'composition gained a weak hook, command or public route' >&2
	exit 1
fi
if rg -q 'callback|context|transport|register.*(route|handler)' \
	"$root/src/cpu/x86/smm_invocation_loader_composition.c" \
	"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
	"$root/src/include/cpu/x86/smm_invocation_loader_composition.h"; then
	printf '%s\n' 'composition gained callback, context or transport coupling' >&2
	exit 1
fi
if git -C "$root" diff --unified=0 "$base" -- src | sed -n 's/^+//p' | \
	rg -q '__weak|APM_CNT|outb|lb_(new_record|add)|mainboard_smi_apmc'; then
	printf '%s\n' 'composition diff gained a weak provider or invocation route' >&2
	exit 1
fi
test "$(rg -c 'smm_invocation_platform_loader_instance_take\(' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_authority.c")" -eq 1
grep -q '^config SMM_INVOCATION_LOADER_COMPOSITION$' \
	"$root/src/cpu/x86/Kconfig"
grep -q 'smm_invocation_loader_compose(published_composition' \
	"$root/src/cpu/x86/smm/smm_module_loader.c"
placement_valid()
{
	source=$1
	snippet=$2
	sed -n '/invocation_composition_started = true/,/return 0;/p' \
		"$source" > "$snippet"
	test "$(grep -c 'invocation_composition_started = true' "$snippet")" -eq 1 &&
	test "$(grep -c 'smm_invocation_loader_compose' "$snippet")" -eq 1 &&
		test "$(grep -c 'goto fail;' "$snippet")" -eq 1 &&
		test "$(grep -c 'return 0;' "$snippet")" -eq 1 &&
		test "$(grep -c 'if (' "$snippet")" -eq 1 &&
		test "$(grep -c ';' "$snippet")" -eq 3
}

loader_source="$root/src/cpu/x86/smm/smm_module_loader.c"
placement="$temporary/loader-placement.c"
placement_valid "$loader_source" "$placement"
placement_mutant="$temporary/smm_module_loader-late-failure.c"
sed '/return 0;/i\
\tif (params)\
\t\tgoto fail;' "$loader_source" > "$placement_mutant"
if placement_valid "$placement_mutant" "$temporary/loader-placement-mutant.c"; then
	printf '%s\n' 'loader placement check accepted a later fallible operation' >&2
	exit 1
fi
placement_call_mutant="$temporary/smm_module_loader-late-call.c"
sed '/return 0;/i\
\t(void)smm_create_map(0, 0, params);' "$loader_source" > \
	"$placement_call_mutant"
if placement_valid "$placement_call_mutant" \
	"$temporary/loader-placement-call-mutant.c"; then
	printf '%s\n' 'loader placement check accepted a later call' >&2
	exit 1
fi
placement_owner_mutant="$temporary/smm_module_loader-late-owner.c"
sed '0,/invocation_composition_started = true;/{/invocation_composition_started = true;/d;}' \
	"$loader_source" > "$temporary/smm_module_loader-no-owner.c"
sed '/smm_invocation_loader_compose(published_composition/,/return 0;/{/return 0;/i\
\tinvocation_composition_started = true;
}' "$temporary/smm_module_loader-no-owner.c" > "$placement_owner_mutant"
if placement_valid "$placement_owner_mutant" \
	"$temporary/loader-placement-owner-mutant.c"; then
	printf '%s\n' 'loader placement accepted late composition ownership' >&2
	exit 1
fi
if rg -q 'smm_invocation_loader_composition_evidence\(' "$root/src" \
	-g '!src/cpu/x86/smm_invocation_loader_composition_gate.c' \
	-g '!src/include/cpu/x86/smm_invocation_loader_composition.h'; then
	printf '%s\n' 'composition evidence gained a bypassing production consumer' >&2
	exit 1
fi

git -C "$root" diff --check
printf '%s\n' 'SMM invocation loader composition tests: PASS'
