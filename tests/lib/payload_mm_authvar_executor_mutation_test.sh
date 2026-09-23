#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"
: > "$tmp/manifest"

compile_and_kill()
{
	name="$1"
	expression="$2"
	mutant="$tmp/executor-$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_executor.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_executor.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/$name-O$optimization"
		log="$tmp/$name-O$optimization.log"
		if ! cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -Wstrict-prototypes \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-builtin -D__TEST__ -D__COREBOOT__ -D__SMM__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/lib" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_executor_acceptance_test.c" \
			"$root/src/lib/payload_mm_authvar.c" \
			"$root/src/lib/payload_mm_authvar_runtime.c" \
			"$root/src/lib/payload_mm_authvar_media.c" "$mutant" \
			"$root/src/lib/payload_mm_authvar_ftw.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_writer.c" \
			-Wl,--wrap=payload_mm_authvar_media_fail_closed \
			-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
			-Wl,--wrap=payload_mm_authvar_media_cache_bind -o "$binary" \
			>"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		printf '%s:%s\n' "$name-O$optimization" "$binary" >> "$tmp/manifest"
	done
}

compile_and_kill queue-header-readback \
	'/result = verify_media(state, queue + 1U, header + 1U/,/result = advance_marker(state, queue/ s/PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE - 1U);/0U);/'
compile_and_kill header-fe-stage \
	's/PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED);/PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);/g'
compile_and_kill queue-record-readback \
	'/result = verify_media(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U/,/result = erase_span(state, geometry->spare_offset/ s/PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE - 1U);/0U);/'
compile_and_kill primary-readback-before-f9 \
	'/result = verify_media(state, geometry->variable_offset, image/,/result = advance_marker(state, queue + PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE/ s/geometry->variable_size);/0U);/'
compile_and_kill record-f9-stage \
	's/PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE);/PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE);/g'
compile_and_kill workspace-spare-fe-stage \
	'/geometry->spare_offset + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET/,/PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);/ s/PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);/PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED);/'
compile_and_kill store-base-translation \
	's/\*base = state->ftw.fv_header_size;/\*base = state->ftw.fv_header_size + 1U;/'

count=0
pids=''
for entry in $(cat "$tmp/manifest"); do
	label=${entry%%:*}
	binary=${entry#*:}
	(
		if ASAN_OPTIONS=detect_leaks=1 "$binary" fault-only \
			>"$tmp/$label.log" 2>&1; then
			echo survived > "$tmp/$label.status"
		else
			echo killed > "$tmp/$label.status"
		fi
	) &
	pids="$pids $!"
	count=$((count + 1))
	if [ "$count" -eq 3 ]; then
		for pid in $pids; do
			wait "$pid"
		done
		count=0
		pids=''
	fi
done
for pid in $pids; do
	wait "$pid"
done
failed=0
for entry in $(cat "$tmp/manifest"); do
	label=${entry%%:*}
	if [ "$(cat "$tmp/$label.status")" != killed ]; then
		echo "ERROR: $label mutant survived" >&2
		cat "$tmp/$label.log" >&2
		failed=1
	fi
done
[ "$failed" -eq 0 ] || exit 1

printf '%s\n' 'Payload-MM authenticated-variable executor integrated mutations: PASS'
