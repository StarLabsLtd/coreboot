#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=c11 -Wall -Wextra -Werror "$@" \
		-I"$root/src/mainboard/emulation/qemu-q35" \
		"$root/tests/lib/q35-dma-policy-test.c" \
		"$root/src/mainboard/emulation/qemu-q35/q35_dma_policy.c" \
		-o "$tmp/$name"
	"$tmp/$name"
}

run_test ordinary
run_test optimized -O2
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined
