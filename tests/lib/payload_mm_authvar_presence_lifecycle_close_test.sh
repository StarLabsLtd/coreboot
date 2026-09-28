#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION 0' > \
	"$temporary/include/config.h"

owner="$root/src/lib/payload_mm_authvar_presence_lifecycle_close.c"
pre_external="$root/src/lib/payload_mm_authvar_presence_pre_external_image_close.c"
payload_failure="$root/src/lib/payload_mm_authvar_presence_payload_failure_close.c"
warm_reset="$root/src/lib/payload_mm_authvar_presence_warm_reset_close.c"
s3_resume="$root/src/lib/payload_mm_authvar_presence_s3_resume_close.c"
reproof="$root/src/lib/payload_mm_authvar_presence_closed_reproof.c"
sources="$owner $pre_external $payload_failure $warm_reset $s3_resume $reproof"

build()
{
	output=$1
	flags=$2
	actual_owner=${3:-$owner}
	# Deliberate normal flag splitting for this strict host harness.
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-pthread $flags -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/lib" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_test.c" \
		"$actual_owner" "$pre_external" "$payload_failure" "$warm_reset" \
		"$s3_resume" "$reproof" -o "$temporary/$output"
}

mutant()
{
	name=$1
	expression=$2
	sed "$expression" "$owner" > "$temporary/$name.c"
	if cmp -s "$owner" "$temporary/$name.c"; then
		printf 'mutant did not change source: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$name-O$optimization"
		if ! build "$binary" "-O$optimization" "$temporary/$name.c"; then
			printf 'lifecycle close mutant did not compile: %s/O%s\n' \
				"$name" "$optimization" >&2
			exit 1
		fi
		if "$temporary/$binary" >/dev/null 2>&1; then
			printf 'lifecycle close mutant survived: %s/O%s\n' \
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

mutant one-restrict \
	'/state_publish(CLOSE_PROVING);/,/after_proof_hook/{s/if (payload_mm_authvar_presence_authority_restrict(/if (false \&\& payload_mm_authvar_presence_authority_restrict(/}'
mutant stale-accepted \
	's/invocation.smi_generation <= trusted.last_invocation_generation/false/'
mutant max-accepted \
	's/invocation.smi_generation == UINT64_MAX/false/; s/trusted->last_invocation_generation != UINT64_MAX/true/'
mutant completion-status \
	'/status = trusted.policy.complete_invocation/,/fail_stop(&trusted)/{s/status != CB_SUCCESS/false/}'
mutant reproof-prerequisite \
	's/return !required ||/return true || required ||/'
mutant source-domain \
	's/source;/((uint32_t)source);/'
mutant source-slot \
	's/!slot_state(slot, SOURCE_CLAIMED)/false/g'

objects=
for source in $sources; do
	object="$temporary/$(basename "$source" .c)-32.o"
	${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
		-Wconversion -Wshadow -ffreestanding -fno-builtin -D__COREBOOT__ \
		-D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/lib" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-c "$source" -o "$object"
	if nm -u "$object" | grep -q '__atomic_'; then
		printf '%s gained a 32-bit libatomic dependency\n' "$source" >&2
		exit 1
	fi
	objects="$objects $object"
done
# Deliberate normal word splitting for the object list.
# shellcheck disable=SC2086
ld -m elf_i386 -r $objects -o "$temporary/lifecycle-close-32.o"

scope_files="$sources $root/src/include/boot/payload_mm_authvar_presence_lifecycle_close.h $root/src/lib/payload_mm_authvar_presence_lifecycle_close_internal.h"
# Deliberate normal word splitting for source files.
# shellcheck disable=SC2086
perl -0777 -pe 's:/\*.*?\*/::gs' $scope_files | \
	sed '/^#if ENV_TEST/,/^#endif/d' > "$temporary/production-scope.c"
if rg -n 'LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED|lifecycle_sealed|coreboot_table|lb_authvar_presence_endpoint|out[bwl]|SMM_APMC|selector|dispatch_locked' \
	"$temporary/production-scope.c"; then
	printf '%s\n' 'lifecycle close owner exceeded its dormant SMM scope' >&2
	exit 1
fi
test "$(sed '/^#if ENV_TEST/,$d' \
	"$root/src/include/boot/payload_mm_authvar_presence_lifecycle_close.h" | \
	rg -o 'payload_mm_authvar_presence_[a-z0-9_]+\(void\);' | wc -l)" -eq 5
if rg -n 'lifecycle_close_source\(' "$temporary/production-scope.c"; then
	printf '%s\n' 'lifecycle close owner exposed a generic source operation' >&2
	exit 1
fi

printf '%s\n' 'Payload-MM presence lifecycle close owner: PASS'
