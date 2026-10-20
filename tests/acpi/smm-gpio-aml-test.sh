#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

top="$1"
out="$2"
mkdir -p "$out"

cc -E -P -x c \
	-I "$top/src/soc/intel/common/block/include" \
	"$top/tests/acpi/smm-gpio.asl" -o "$out/smm-gpio.asl"
# Mock only the hardware SMI boundary; the methods remain the production ASL.
sed -i 's/APMC = 0xe8/GSMI ()/' "$out/smm-gpio.asl"
iasl -tc "$out/smm-gpio.asl" >/dev/null

run_method()
{
	commands="$1"
	expected="$2"
	output=$(acpiexec -b "$commands" "$out/smm-gpio.aml" 2>&1)
	printf '%s\n' "$output" | grep -F "$expected" >/dev/null
}

run_method 'set N \_SB.PCI0.TSTS 0; set N \_SB.PCI0.TSVL 0; execute \_SB.PCI0.PGTX 1' \
	'[Integer] = 0000000000000000'
run_method 'set N \_SB.PCI0.TSTS 0; set N \_SB.PCI0.TSVL 1; execute \_SB.PCI0.PGTX 1' \
	'[Integer] = 0000000000000001'
run_method 'set N \_SB.PCI0.TSTS 1; set N \_SB.PCI0.TSVL 0; execute \_SB.PCI0.PGTX 1' \
	'[Integer] = 0000000000000002'
run_method 'set N \_SB.PCI0.TSTS 1; execute \_SB.PCI0.PSTX 1' \
	'[Integer] = 0000000000000001'
run_method 'set N \_SB.PCI0.TSTS 1; execute \_SB.PCI0.PCTX 1' \
	'[Integer] = 0000000000000001'
run_method 'set N \_SB.PCI0.TNRP 1; execute \_SB.PCI0.PGTX 1' \
	'[Integer] = 0000000000000002'
run_method 'set N \_SB.PCI0.TSTS 1; execute \_SB.PCI0.PGTX 1; set N \_SB.PCI0.TSTS 0; set N \_SB.PCI0.TSVL 1; execute \_SB.PCI0.PGTX 1' \
	'[Integer] = 0000000000000001'
