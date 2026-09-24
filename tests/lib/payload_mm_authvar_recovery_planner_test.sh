#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER 1' > \
	"$temporary/include/config.h"

cases='planner-zero-write planner-execute planner-snapshot-mutation
planner-geometry-mutation planner-step-mutation planner-token-mutation
planner-callback-image-mutation planner-abort-read-image-mutation
planner-replay-read-image-mutation'

build_and_run()
{
	output=$1
	source=$2
	flags=$3
	"${CC:-cc}" -std=gnu11 $flags -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_test.c" "$source" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -o "$output"
	for case_name in $cases; do
		ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$output" "$case_name"
	done
}

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined'; do
	build_and_run "$temporary/test" \
		"$root/src/lib/payload_mm_authvar_executor.c" "$flags"
done

mutant()
{
	name=$1
	expression=$2
	case_name=$3
	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > \
		"$temporary/$name.c"
	! cmp -s "$root/src/lib/payload_mm_authvar_executor.c" \
		"$temporary/$name.c"
	build_and_run "$temporary/$name" "$temporary/$name.c" '-O2' >/dev/null 2>&1 && {
		echo "ERROR: $name mutation survived" >&2
		exit 1
	}
	: "$case_name"
}

mutant snapshot-binding \
	's/verify_media(state, 0, snapshot(), state->contract.store_size) !=/PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS !=/' \
	planner-snapshot-mutation
mutant token-binding \
	's/state->recovery.token != state->token/false/' planner-token-mutation
mutant step-binding \
	's/memcmp(\&observed,/memcmp(\&state->recovery.steps[state->recovery_count],/' \
	planner-step-mutation
mutant canonical-image-callback-binding \
	's/PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS || !recovery_image_unchanged(state))/PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/' \
	planner-callback-image-mutation
mutant abort-read-image-binding \
	's/if (!recovery_read_valid(state))/if (false)/' \
	planner-abort-read-image-mutation
mutant replay-read-image-binding \
	's/if (!recovery_read_valid(state))/if (false)/' \
	planner-replay-read-image-mutation

grep -q '^config PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER$' "$root/src/lib/Kconfig"
grep -q 'CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)' \
	"$root/src/lib/payload_mm_authvar_executor.c"

echo 'Payload-MM authvar read-only recovery planner tests: PASS'
