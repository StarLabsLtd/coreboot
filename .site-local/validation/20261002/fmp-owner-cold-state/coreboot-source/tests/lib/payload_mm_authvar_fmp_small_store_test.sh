#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' > "$temporary/include/config.h"
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1

awk '
/^static uint64_t fmp_mutate\(/ { active = 1 }
active && /state->policy.maximum_data_size = MIN/ {
	if ($0 != "\tstate->policy.maximum_data_size = MIN(state->policy.maximum_data_size,") exit 1
	if (getline <= 0 || $0 != "\t\tstate->policy.maximum_record_size - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE);") exit 1
	print "\t/* FMP_SMALL_STORE_CLAMP_DISCARDED */"
	count++
	next
}
{ print }
END { if (count != 1) exit 1 }
' "$root/src/lib/payload_mm_authvar_executor.c" > "$temporary/discard.c"
awk '
$0 == "\t/* FMP_SMALL_STORE_CLAMP_DISCARDED */" {
	print "\tstate->policy.maximum_data_size = MIN(state->policy.maximum_data_size,"
	print "\t\tstate->policy.maximum_record_size - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE);"
	next
}
{ print }
' "$temporary/discard.c" > "$temporary/restored.c"
cmp "$temporary/restored.c" "$root/src/lib/payload_mm_authvar_executor.c"

for optimization in 0 2; do
	for variant in normal discard; do
		executor="$root/src/lib/payload_mm_authvar_executor.c"
		if [ "$variant" = discard ]; then executor="$temporary/discard.c"; fi
		set -- "$root/tests/lib/payload_mm_authvar_executor_test.c" \
			"$root/tests/lib/payload_mm_authvar_fmp_executor_stubs.c" "$executor"
		for source in coordinator set_preflight controlled_mode view authority candidate \
			bundle certdb mode format route fv ftw store store_semantics record writer service; do
			set -- "$@" "$root/src/lib/payload_mm_authvar_$source.c"
		done
		"${HOSTCC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wshadow -Wstrict-prototypes -fno-builtin -fno-pie -no-pie \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-D__TEST__ -D__COREBOOT__ -D__SMM__ \
			-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
			"$@" "$root/src/lib/payload_mm_fmp_state.c" \
			"$root/src/lib/payload_mm_fmp_owner.c" -o "$temporary/$variant-O$optimization"
		if [ "$variant" = normal ]; then
			"$temporary/$variant-O$optimization" fmp-initialize-small-store
		else
			set +e
			"$temporary/$variant-O$optimization" fmp-initialize-small-store \
				> "$temporary/negative.log" 2>&1
			status=$?
			set -e
			test "$status" -eq 134
			grep -q 'status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS' "$temporary/negative.log"
			if grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
				"$temporary/negative.log"; then exit 1; fi
		fi
		printf 'FMP small-store O%s %s: PASS\n' "$optimization" "$variant"
	done
done
