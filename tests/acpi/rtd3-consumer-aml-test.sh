#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

top="$1"
out="$2"
generator="$3"
mkdir -p "$out"

RTD3_AML_DIR="$out" "$generator" >/dev/null

result()
{
	table="$1"
	commands="$2"
	output=$(acpiexec -b "$commands" "$out/$table.aml" 2>&1)
	printf '%s\n' "$output" | sed -n 's/^.*\[Integer\] = 0*\([0-9A-Fa-f][0-9A-Fa-f]*\)$/\1/p' | tail -1
}

expect()
{
	table="$1"
	commands="$2"
	expected="$3"
	actual=$(result "$table" "$commands")
	[ "$actual" = "$expected" ] || {
		echo "$table: expected $expected, got $actual" >&2
		return 1
	}
}

# Public _STA remains Boolean; private PSTA preserves a failed read as 2.
expect enable-high 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' 0
expect enable-high 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' 1
expect enable-low 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' 1
expect enable-low 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' 0
expect reset-high 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' 1
expect reset-high 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' 0
expect reset-low 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' 0
expect reset-low 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' 1
expect enable-high 'set N \_SB.PCI0.GERR 1; execute \_SB.PCI0.RP01.RTD3.PSTA' 2
expect enable-high 'set N \_SB.PCI0.GERR 1; execute \_SB.PCI0.RP01.RTD3._STA' 0

# A failed initial status read must not start either GPIO operation or OPAL unlock.
expect enable-high 'set N \_SB.PCI0.GERR 1; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.SCNT' 0
expect enable-high 'set N \_SB.PCI0.GERR 1; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.RP01.RTD3.APMC' 0
expect enable-high 'set N \_SB.PCI0.GERR 1; execute \_SB.PCI0.RP01.RTD3._ON; set N \_SB.PCI0.GERR 0; set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.SCNT' 2

# A failed first setter stops the sequence; a subsequent invocation still acquires the mutex.
expect enable-high 'set N \_SB.PCI0.TVAL 0; set N \_SB.PCI0.SERR 1; execute \_SB.PCI0.RP01.RTD3._ON; set N \_SB.PCI0.SERR 0; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.SCNT' 3

# Standard _ON still completes normally and returns an ignored Integer on GPIO failure.
output=$(acpiexec -b 'set N \_SB.PCI0.TVAL 0; set N \_SB.PCI0.SERR 1; execute \_SB.PCI0.RP01.RTD3._ON' "$out/enable-high.aml" 2>&1)
printf '%s\n' "$output" | grep -F 'Evaluation of \_SB.PCI0.RP01.RTD3._ON returned object' >/dev/null
printf '%s\n' "$output" | grep -F '[Integer] = 0000000000000000' >/dev/null

# Production Bluetooth AML stops on failed status/reset operations and releases CNMT.
expect enable-high 'set N \_SB.PCI0.GERR 1; execute \_SB.PCI0.RP01.BT00.BTRT._ON; execute \_SB.PCI0.SCNT' 0
expect enable-high 'set N \_SB.PCI0.SERR 1; execute \_SB.PCI0.RP01.BT00.BTRT._RST; set N \_SB.PCI0.SERR 0; execute \_SB.PCI0.RP01.BT00.BTRT._RST; execute \_SB.PCI0.SCNT' 3

# The unaffected direct GPIO route remains functional.
expect direct 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' 1
