#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

for optimization in 0 2; do
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_ftw_test.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" -o "$tmp/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization"
done

mutation_test()
{
	name="$1"
	expression="$2"
	mutant="$tmp/payload_mm_authvar_ftw-$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_ftw.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_ftw.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		log="$tmp/mutant-$name-O$optimization.log"
		if ! cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wconversion -Wshadow -Wstrict-prototypes \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-builtin -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_ftw_test.c" \
			"$root/src/lib/payload_mm_authvar_fv.c" "$mutant" \
			-o "$binary" >"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutation_test workspace-crc \
	's/== crc32(canonical/!= crc32(canonical/'
mutation_test committed-state \
	's/header_state != PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE)/header_state != 0xfbU \&\& header_state != PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE)/'
mutation_test dirty-tail \
	's/if (header\[0\] == PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED) {/if (false \&\& header[0] == PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED) {/'
mutation_test queue-threshold \
	's/offset < PAYLOAD_MM_AUTHVAR_FTW_WRITE/offset <= PAYLOAD_MM_AUTHVAR_FTW_WRITE/'
mutation_test restore-validation \
	's/old_action == PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED/old_action == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE/'
mutation_test record-bounds \
	's/if (!record_bounds_valid/if (false \&\& !record_bounds_valid/'
mutation_test stale-spare-valid \
	's/(spare_valid \&\& spare_tail_erased \&\&/(false \&\& spare_tail_erased \&\&/'
mutation_test stale-spare-erased-subset \
	's/spare_fv_erased_subset = memcmp/spare_fv_erased_subset = false \&\& memcmp/'
mutation_test stale-spare-erased-tail \
	's/spare_tail_erased = bytes_are(/spare_tail_erased = true || bytes_are(/'
mutation_test stale-spare-action-gate \
	's/candidate.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE)/false)/'
mutation_test uncommitted-workspace-subset \
	's/if ((workspace\[i\] \& expected) != expected)/if (false \&\& (workspace[i] \& expected) != expected)/'
mutation_test torn-workspace-byte \
	's/(workspace\[i\] \& expected) != expected/workspace[i] != expected/'
mutation_test torn-erase-byte \
	's/(candidate\[i\] \& authoritative\[i\]) != authoritative\[i\]/candidate[i] != authoritative[i]/'

grep -q '^# oracle StarLabsLtd/edk2 26.09 aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272$' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
grep -q '^working-header[[:space:]]32[[:space:]]20[[:space:]]0[[:space:]]24' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
grep -q '^write-header[[:space:]]40[[:space:]]0[[:space:]]4[[:space:]]24[[:space:]]32' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
grep -q '^write-record[[:space:]]40[[:space:]]0[[:space:]]-[[:space:]]8[[:space:]]16[[:space:]]24[[:space:]]32$' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
grep -q '^# caller coreboot C4A52EF3-4E27-47E2-950B-FDAAB521B895$' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
awk '/payload_mm_authvar_ftw[.]c/ && \
     $0 !~ /CONFIG_PAYLOAD_MM_AUTHVAR_(FTW_DECODER|MOR_ENTRY_PROBE)/ \
     { bad = 1 } END { exit bad }' \
	"$root/src/lib/Makefile.mk"
printf '%s\n' 'Payload-MM EDK2-compatible FV/FTW decoder and mutation tests: PASS'
