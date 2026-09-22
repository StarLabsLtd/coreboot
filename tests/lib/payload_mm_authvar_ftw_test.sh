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

grep -q '^# oracle StarLabsLtd/edk2 26.09 aab7b589fc59b7e2b8fb7eb79519bf1a5e5a5272$' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
grep -q '^working-header[[:space:]]32[[:space:]]20[[:space:]]0[[:space:]]24' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
grep -q '^write-header[[:space:]]40[[:space:]]0[[:space:]]4[[:space:]]24[[:space:]]32' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
grep -q '^write-record[[:space:]]40[[:space:]]0[[:space:]]-[[:space:]]8[[:space:]]16[[:space:]]24[[:space:]]32$' \
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_2609_layout.tsv"
awk '/payload_mm_authvar_ftw[.]c/ && $0 !~ /CONFIG_PAYLOAD_MM_AUTHVAR_FTW_DECODER/ { bad = 1 } END { exit bad }' \
	"$root/src/lib/Makefile.mk"
printf '%s\n' 'Payload-MM EDK2-compatible FV/FTW decoder tests: PASS'
