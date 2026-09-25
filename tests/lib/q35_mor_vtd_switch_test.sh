#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-mor-vtd-switch.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

for optimization in 0 2; do
	output=$temporary/test-O$optimization
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-I"$root/src/mainboard/emulation/qemu-q35" \
		"$root/tests/lib/q35_mor_vtd_switch_test.c" \
		"$root/src/mainboard/emulation/qemu-q35/vtd_registers.c" \
		-o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

printf '%s\n' 'Q35 MOR VT-d root switch tests: PASS'
