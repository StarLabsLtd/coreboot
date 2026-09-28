#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

composition="$root/src/lib/payload_mm_authvar_presence_lifecycle_composition.c"
registration="$root/src/lib/payload_mm_authvar_presence_lifecycle_registration.c"
pre_external="$root/src/lib/payload_mm_authvar_presence_pre_external_image_registration.c"
payload_failure="$root/src/lib/payload_mm_authvar_presence_payload_failure_registration.c"
warm_reset="$root/src/lib/payload_mm_authvar_presence_warm_reset_registration.c"
s3_resume="$root/src/lib/payload_mm_authvar_presence_s3_resume_registration.c"
sources="$composition $registration $pre_external $payload_failure $warm_reset $s3_resume"

build()
{
	output=$1
	flags=$2
	actual_composition=${3:-$composition}
	actual_registration=${4:-$registration}
	actual_pre_external=${5:-$pre_external}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_presence_lifecycle_composition_test.c" \
		"$actual_composition" "$actual_registration" "$actual_pre_external" \
		"$payload_failure" "$warm_reset" "$s3_resume" \
		-o "$temporary/$output"
}

mutant()
{
	name=$1
	component=$2
	expression=$3
	case "$component" in
	composition)
		source=$composition
		mutant_composition="$temporary/$name.c"
		mutant_registration=$registration
		mutant_pre_external=$pre_external
		;;
	registration)
		source=$registration
		mutant_composition=$composition
		mutant_registration="$temporary/$name.c"
		mutant_pre_external=$pre_external
		;;
	pre_external)
		source=$pre_external
		mutant_composition=$composition
		mutant_registration=$registration
		mutant_pre_external="$temporary/$name.c"
		;;
	esac
	sed "$expression" "$source" > "$temporary/$name.c"
	if cmp -s "$source" "$temporary/$name.c"; then
		printf 'mutant did not change source: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$name-O$optimization"
		build "$binary" "-O$optimization" "$mutant_composition" \
			"$mutant_registration" "$mutant_pre_external"
		if "$temporary/$binary" >/dev/null 2>&1; then
			printf 'lifecycle installation mutant survived: %s/O%s\n' \
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

mutant composition-inverse composition \
	's/__atomic_load_n(&composition_state_inverse, __ATOMIC_ACQUIRE)/~(uint32_t)COMPOSITION_READY/'
mutant composition-preclaim-inverse composition \
	's/if (inverse != ~(uint32_t)COMPOSITION_EMPTY)/if (false)/'
mutant composition-postclaim-inverse composition \
	'/__atomic_load_n(&composition_state_inverse, __ATOMIC_ACQUIRE) !=/,/inverse) {/c\
\t    false) {'
mutant composition-postclaim-state composition \
	'/if (claimed_action)/,/inverse) {/{s/__atomic_load_n(&composition_state, __ATOMIC_ACQUIRE)/COMPOSITION_BUSY/;}'
mutant source-consumed composition \
	'/bool payload_mm_authvar_presence_installation_evidence_complete/,/^}/{s/payload_mm_authvar_presence_s3_resume_consumed()/true/}'
mutant duplicate-poison registration \
	'0,/poison(slot);/{s/poison(slot);/(void)slot;/}'
mutant sealed-copy registration \
	's/value.sealed_evidence == value.evidence/true/'
mutant consumed-zero registration \
	'/bool payload_mm_authvar_presence_lifecycle_registration_consumed/,/^}/{s/snapshot_zero(&value)/((void)value, true)/}'
mutant explicit-scrub registration \
	's/__atomic_store_n(&slot->sealed_evidence, 0,/__atomic_store_n(\&slot->sealed_evidence, 1,/'
mutant dirty-empty-preclaim registration \
	's/if (!snapshot_zero(&before))/if (((void)before, false))/; s/memcmp(&before, &after, sizeof(before))/((void)before, (void)after, false)/; s/!snapshot_zero(&after)/((void)after, false)/'
mutant postclaim-snapshot registration \
	's/memcmp(&before, &after, sizeof(before))/((void)before, (void)after, false)/; s/!snapshot_zero(&after)/((void)after, false)/'
mutant postclaim-state registration \
	'/after = snapshot(slot);/,/!snapshot_zero/{s/__atomic_load_n(&slot->state, __ATOMIC_ACQUIRE)/REGISTRATION_ISSUING/;}'
mutant wrong-source-evidence pre_external \
	'0,/&pre_external_image_registration, PRE_EXTERNAL_IMAGE_EVIDENCE);/{s/PRE_EXTERNAL_IMAGE_EVIDENCE);/0x57525354U);/;}'

objects=
for source in $sources; do
	object="$temporary/$(basename "$source" .c)-32.o"
	${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
		-Wconversion -Wshadow -ffreestanding -fno-builtin -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -c "$source" -o "$object"
	if nm -u "$object" | grep -q '__atomic_'; then
		printf '%s gained a 32-bit libatomic dependency\n' "$source" >&2
		exit 1
	fi
	objects="$objects $object"
done
# Deliberate normal word splitting for the object list.
# shellcheck disable=SC2086
ld -m elf_i386 -r $objects -o "$temporary/lifecycle-installation-32.o"

public_header="$root/src/include/boot/payload_mm_authvar_presence_lifecycle_composition.h"
private_header="$root/src/lib/payload_mm_authvar_presence_lifecycle_registration.h"
production_sources="$composition $registration $pre_external $payload_failure $warm_reset $s3_resume"
scope_files="$production_sources $public_header $private_header"
# Strip comments so the contract's explicit prohibition text is not a match.
# Deliberate normal word splitting for the file list.
# shellcheck disable=SC2086
perl -0777 -pe 's:/\*.*?\*/::gs' $scope_files | \
	sed '/^#if ENV_TEST/,/^#endif/d' > "$temporary/production-scope.c"
if rg -n 'LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED|lifecycle_sealed|endpoint|transport|hook|selector' \
	"$temporary/production-scope.c"; then
	printf '%s\n' 'lifecycle installation evidence exceeded its scope' >&2
	exit 1
fi
test "$(rg -o 'payload_mm_authvar_presence_[a-z0-9_]+_register\(void\)' \
	"$root/src/include/boot/payload_mm_authvar_presence_lifecycle_composition.h" | \
	wc -l)" -eq 4
if rg -n 'enum .*source|\[[^]]+\]' "$temporary/production-scope.c"; then
	printf '%s\n' 'lifecycle installation gained a selector or caller array' >&2
	exit 1
fi
if rg -n 'installation_evidence_(compose|complete)' \
	"$root/src/lib/payload_mm_authvar_presence_producer.c" \
	"$root/src/include/boot/payload_mm_authvar_presence_producer.h"; then
	printf '%s\n' 'installation evidence entered producer closure policy' >&2
	exit 1
fi

printf '%s\n' 'Payload-MM presence lifecycle installation evidence: PASS'
