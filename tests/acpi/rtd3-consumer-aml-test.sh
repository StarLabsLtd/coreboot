#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

top="$1"
out="$2"
generator="$3"
no_smm_generator="$4"
mkdir -p "$out"
mkdir -p "$out/no-smm"

RTD3_AML_DIR="$out" "$generator" >/dev/null
RTD3_AML_DIR="$out/no-smm" "$no_smm_generator" >/dev/null

run_eval()
{
	table="$1"
	commands="$2"
	shift 2
	transcript="$out/${table%/*}.${table##*/}.transcript"
	if ! timeout 15s acpiexec -b "$commands" "$out/$table.aml" >"$transcript" 2>&1; then
		cat "$transcript" >&2
		return 1
	fi
	if ! "$top/tests/acpi/acpiexec-eval-check.py" "$transcript" "$@"; then
		cat "$transcript" >&2
		return 1
	fi
}

# Public _STA remains Boolean; private PSTA preserves a failed read as 2.
run_eval enable-high 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:0'
run_eval enable-high 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:1'
run_eval enable-low 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:1'
run_eval enable-low 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:0'
run_eval reset-high 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:1'
run_eval reset-high 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:0'
run_eval reset-low 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:0'
run_eval reset-low 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:1'
run_eval enable-high 'set N \_SB.PCI0.PERR 1; execute \_SB.PCI0.RP01.RTD3.PSTA' '\_SB.PCI0.RP01.RTD3.PSTA=integer:2'
run_eval enable-high 'set N \_SB.PCI0.PERR 1; execute \_SB.PCI0.RP01.RTD3._STA' '\_SB.PCI0.RP01.RTD3._STA=integer:0'

# A failed initial status read must not start either GPIO operation or OPAL unlock.
run_eval enable-high \
	'set N \_SB.PCI0.PERR 1; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.SCNT' \
	'\_SB.PCI0.RP01.RTD3._ON=integer:0' '\_SB.PCI0.SCNT=integer:0'
run_eval enable-high \
	'set N \_SB.PCI0.PERR 1; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.RP01.RTD3.APMC' \
	'\_SB.PCI0.RP01.RTD3._ON=integer:0' '\_SB.PCI0.RP01.RTD3.APMC=integer:0'
run_eval enable-high \
	'set N \_SB.PCI0.PERR 1; execute \_SB.PCI0.RP01.RTD3._ON; set N \_SB.PCI0.PERR 0; set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.SCNT' \
	'\_SB.PCI0.RP01.RTD3._ON=integer:0' '\_SB.PCI0.RP01.RTD3._ON=none' \
	'\_SB.PCI0.SCNT=integer:2'

# A failed first setter stops the sequence; a subsequent invocation still acquires the mutex.
run_eval enable-high \
	'set N \_SB.PCI0.TVAL 0; set N \_SB.PCI0.SERR 1; execute \_SB.PCI0.RP01.RTD3._ON; set N \_SB.PCI0.SERR 0; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.SCNT' \
	'\_SB.PCI0.RP01.RTD3._ON=integer:0' '\_SB.PCI0.RP01.RTD3._ON=none' \
	'\_SB.PCI0.SCNT=integer:3'

# This captures the unresolved standard PowerResource completion contract.
run_eval enable-high \
	'set N \_SB.PCI0.TVAL 0; set N \_SB.PCI0.SERR 1; execute \_SB.PCI0.RP01.RTD3._ON' \
	'\_SB.PCI0.RP01.RTD3._ON=integer:0'

# Production Bluetooth AML stops on failed status/reset operations and releases CNMT.
run_eval enable-high \
	'set N \_SB.PCI0.PERR 1; execute \_SB.PCI0.RP01.BT00.BTRT._ON; execute \_SB.PCI0.SCNT' \
	'\_SB.PCI0.RP01.BT00.BTRT._ON=integer:0' '\_SB.PCI0.SCNT=integer:0'
run_eval enable-high \
	'set N \_SB.PCI0.SERR 1; execute \_SB.PCI0.RP01.BT00.BTRT._RST; set N \_SB.PCI0.SERR 0; execute \_SB.PCI0.RP01.BT00.BTRT._RST; execute \_SB.PCI0.SCNT' \
	'\_SB.PCI0.RP01.BT00.BTRT._RST=integer:0' '\_SB.PCI0.RP01.BT00.BTRT._RST=none' \
	'\_SB.PCI0.SCNT=integer:3'

# The unaffected direct GPIO route remains functional.
run_eval direct 'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA' \
	'\_SB.PCI0.RP01.RTD3._STA=integer:1'

# A true feature-disabled build routes even allowlisted pads through the direct methods.
run_eval no-smm/enable-high \
	'set N \_SB.PCI0.PERR 1; set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3.PSTA' \
	'\_SB.PCI0.RP01.RTD3.PSTA=integer:1'
