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
	if ! cc -std=gnu11 -O0 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_ftw_test.c" "$mutant" \
		-o "$tmp/mutant-$name" >"$tmp/mutant-$name.log" 2>&1; then
		echo "ERROR: $name mutant did not compile" >&2
		cat "$tmp/mutant-$name.log" >&2
		exit 1
	fi
	if ASAN_OPTIONS=detect_leaks=1 "$tmp/mutant-$name" >/dev/null 2>&1; then
		echo "ERROR: $name mutant survived" >&2
		exit 1
	fi
}

mutation_test workspace-crc \
	's/== crc32(canonical/!= crc32(canonical/'
mutation_test committed-state \
	's/ != 0xf8U)/ != 0xfbU \&\& header_state != 0xf8U)/'
mutation_test dirty-tail \
	's/if (header\[0\] == 0xffU) {/if (false \&\& header[0] == 0xffU) {/'
mutation_test queue-threshold \
	's/offset < FTW_WRITE/offset <= FTW_WRITE/'
mutation_test restore-validation \
	's/old_action == PAYLOAD_MM_AUTHVAR_FTW_FAIL_CLOSED/old_action == PAYLOAD_MM_AUTHVAR_FTW_INITIALIZE_WORKSPACE/'
mutation_test record-bounds \
	's/if (!record_bounds_valid/if (false \&\& !record_bounds_valid/'

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
awk '/payload_mm_authvar_ftw[.]c/ && $0 !~ /CONFIG_PAYLOAD_MM_AUTHVAR_FTW_DECODER/ { bad = 1 } END { exit bad }' \
	"$root/src/lib/Makefile.mk"
printf '%s\n' 'Payload-MM EDK2-compatible FV/FTW decoder and mutation tests: PASS'
