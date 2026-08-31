#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

grep -Fq 'ts_cbmem_table->tick_freq_mhz = timestamp_tick_freq_mhz();' \
	"$root/src/lib/timestamp.c"
grep -Fq 'tsc_cache_measured_frequency(&timer_tsc, calibrate_tsc_with_pit());' \
	"$root/src/drivers/pc80/pc/i8254.c"
cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
	-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -fno-builtin \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$root/build" \
	"$root/tests/lib/x86_timestamp_frequency_standalone_test.c" \
	"$root/src/arch/x86/timestamp.c" -o "$temporary/x86-timestamp-frequency-test"
"$temporary/x86-timestamp-frequency-test"
printf '%s\n' 'x86 timestamp frequency standalone tests: PASS'
