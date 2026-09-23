#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"
: > "$tmp/manifest"

compile_mutant()
{
	name="$1"
	expression="$2"
	test_case="$3"
	mutant="$tmp/executor-$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	set -- $(git diff --no-index --numstat \
		"$root/src/lib/payload_mm_authvar_executor.c" "$mutant" || true)
	if [ "$1" != 1 ] || [ "$2" != 1 ]; then
		echo "ERROR: $name mutant did not change exactly one line" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/$name-O$optimization"
		log="$tmp/$name-O$optimization.log"
		executor_source="$mutant"
		include_flag=''
		if [ "$name" = marker-nor-clear ]; then
			executor_source=''
			include_flag="-DEXECUTOR_SOURCE_INCLUDE=\"$mutant\""
		fi
		if ! cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -Wstrict-prototypes \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-builtin -D__TEST__ -D__COREBOOT__ -DEXECUTOR_SCAN_WRAP \
			$include_flag \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_executor_test.c" $executor_source \
			"$root/src/lib/payload_mm_authvar_ftw.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_record.c" \
			"$root/src/lib/payload_mm_authvar_writer.c" \
			-Wl,--wrap=payload_mm_authvar_store_scan -o "$binary" \
			>"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		printf '%s:%s:%s\n' "$name-O$optimization" "$binary" "$test_case" \
			>> "$tmp/manifest"
	done
}

compile_special_baselines()
{
	for optimization in 0 2; do
		common="$root/src/lib/payload_mm_authvar_ftw.c
$root/src/lib/payload_mm_authvar_store.c
$root/src/lib/payload_mm_authvar_store_semantics.c
$root/src/lib/payload_mm_authvar_record.c
$root/src/lib/payload_mm_authvar_writer.c"
		base="$tmp/baseline-scan-O$optimization"
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -Wstrict-prototypes \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-builtin -D__TEST__ -D__COREBOOT__ -DEXECUTOR_SCAN_WRAP \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_executor_test.c" \
			"$root/src/lib/payload_mm_authvar_executor.c" $common \
			-Wl,--wrap=payload_mm_authvar_store_scan -o "$base"
		ASAN_OPTIONS=detect_leaks=1 "$base" reclaim-precommit-scan

		base="$tmp/baseline-marker-O$optimization"
		include_flag="-DEXECUTOR_SOURCE_INCLUDE=\"$root/src/lib/payload_mm_authvar_executor.c\""
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -Wstrict-prototypes \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-builtin -D__TEST__ -D__COREBOOT__ $include_flag \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_executor_test.c" $common \
			-o "$base"
		ASAN_OPTIONS=detect_leaks=1 "$base" marker-nor-clear
	done
}

compile_special_baselines

compile_mutant marker-expected-state \
	'/static enum payload_mm_authvar_media_result marker(/,/^}/ s/if (\*byte != expected ||/if (false ||/' \
	marker-expected-state
compile_mutant marker-nor-clear \
	'/static enum payload_mm_authvar_media_result marker(/,/^}/ s/wanted != (expected \& wanted)/false/' \
	marker-nor-clear
compile_mutant direct-step-order \
	'/static enum payload_mm_authvar_media_result execute_direct(/,/^}/ s/i++)/i += 2U)/' \
	direct-add
compile_mutant workspace-old-invalidate \
	'/static enum payload_mm_authvar_media_result workspace_rebuild(/,/^}/ s/if (invalidate_working)/if (false \&\& invalidate_working)/' \
	workspace-invalidate-order
compile_mutant abort-spare-readback \
	'/static enum payload_mm_authvar_media_result recover_abort(/,/^}/ s/if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (false \&\& result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/' \
	abort-spare-readback
compile_mutant replay-spare-readback \
	'/result = verify_media(state, geometry->spare_offset, image,/,/result = erase_span(state, geometry->variable_offset/ s/if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (false \&\& result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/' \
	replay-spare-readback
compile_mutant replay-primary-readback \
	'/static enum payload_mm_authvar_media_result replay_spare(/,/^}/ { /result = verify_media(state, geometry->variable_offset, image,/,/result = marker(state, queue/ s/if (result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/if (false \&\& result != PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS)/; }' \
	replay-primary-readback
compile_mutant complete-before-cleanup \
	'/case PAYLOAD_MM_AUTHVAR_FTW_COMPLETE_NEW:/,/^\t}/ s/PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);/PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);/' \
	complete
compile_mutant restore-queue-before-cleanup \
	'/static enum payload_mm_authvar_media_result restore_workspace(/,/^}/ s/PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE);/PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);/' \
	restore-queue-order
compile_mutant cleanup-spare-readback \
	'/case PAYLOAD_MM_AUTHVAR_FTW_CLEANUP_SPARE:/,/^\t}/ s/return result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ?/return false ?/' \
	cleanup-spare-readback
compile_mutant recovery-fresh-snapshot \
	'/static uint64_t recover_session(/,/^}/ s/result = snapshot_read(state);/result = state->recovery_count ? PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS : snapshot_read(state);/' \
	initialize
compile_mutant reclaim-precommit-scan \
	'/static enum payload_mm_authvar_media_result execute_reclaim(/,/^contradiction:/ s/) != CB_SUCCESS ||/), false ||/' \
	reclaim-precommit-scan

failed=0
while IFS=: read -r label binary test_case; do
	if ASAN_OPTIONS=detect_leaks=1 "$binary" "$test_case" >/dev/null 2>&1; then
		echo "SURVIVED: $label ($test_case)" >&2
		failed=1
	else
		echo "KILLED: $label ($test_case)"
	fi
done < "$tmp/manifest"
[ "$failed" -eq 0 ] || exit 1

printf '%s\n' 'Payload-MM authenticated-variable recovery mutations: PASS'
