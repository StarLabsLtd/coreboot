#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
	-I"$root/src/mainboard/emulation/qemu-q35" \
	"$root/tests/lib/q35-vtd-registers-test.c" \
	"$root/src/mainboard/emulation/qemu-q35/vtd_registers.c" \
	-o "$tmp/q35-vtd-registers-test"
"$tmp/q35-vtd-registers-test"
