#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
	-D__TEST__ -D__COREBOOT__ \
	-D__RAMSTAGE__ -fno-builtin -include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$root/build" \
	-I"$root/src/mainboard/emulation/qemu-q35" \
	"$root/tests/mainboard/q35-lapic-timer-test.c" \
	"$root/src/mainboard/emulation/qemu-q35/lapic_timer.c" \
	-o "$temporary/q35-lapic-timer-test"
"$temporary/q35-lapic-timer-test"
printf '%s\n' 'q35 LAPIC timer host tests: PASS'
