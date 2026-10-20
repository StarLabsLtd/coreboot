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

iasl -d -p "$out/enable-high" "$out/enable-high.aml" >/dev/null
iasl -d -p "$out/bt-optional" "$out/bt-optional.aml" >/dev/null
python3 "$top/tests/acpi/bluetooth-reset-sequence-check.py" "$out/enable-high.dsl" plain
python3 "$top/tests/acpi/bluetooth-reset-sequence-check.py" "$out/bt-optional.dsl" optional

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
	if ! python3 "$top/tests/acpi/acpiexec-eval-check.py" "$transcript" "$@"; then
		cat "$transcript" >&2
		return 1
	fi
}

# The unchanged GET helper selects the protected route only for allowlisted pads.
run_eval enable-high \
	'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA; execute \_SB.PCI0.PGCT; execute \_SB.PCI0.DGCT' \
	'\_SB.PCI0.RP01.RTD3._STA=integer:1' '\_SB.PCI0.PGCT=integer:1' \
	'\_SB.PCI0.DGCT=integer:0'
run_eval direct \
	'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA; execute \_SB.PCI0.PGCT; execute \_SB.PCI0.DGCT' \
	'\_SB.PCI0.RP01.RTD3._STA=integer:1' '\_SB.PCI0.PGCT=integer:0' \
	'\_SB.PCI0.DGCT=integer:1'

# A true feature-disabled build routes the same allowlisted pad directly.
run_eval no-smm/enable-high \
	'set N \_SB.PCI0.TVAL 1; execute \_SB.PCI0.RP01.RTD3._STA; execute \_SB.PCI0.PGCT; execute \_SB.PCI0.DGCT' \
	'\_SB.PCI0.RP01.RTD3._STA=integer:1' '\_SB.PCI0.PGCT=integer:0' \
	'\_SB.PCI0.DGCT=integer:1'

# Polarity remains in the common helper and produces the baseline logical state.
run_eval enable-high 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' \
	'\_SB.PCI0.RP01.RTD3._STA=integer:0'
run_eval enable-low 'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._STA' \
	'\_SB.PCI0.RP01.RTD3._STA=integer:1'

# RTD3 uses its baseline helpers and sequence. 0x15 is SET pad 10 and 0x16 is
# CLEAR pad 11. The feature-disabled build produces the same operation trace.
run_eval enable-high \
	'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.EVTS; execute \_SB.PCI0.PCNT; execute \_SB.PCI0.DCNT' \
	'\_SB.PCI0.RP01.RTD3._ON=none' '\_SB.PCI0.EVTS=integer:0x1516' \
	'\_SB.PCI0.PCNT=integer:2' '\_SB.PCI0.DCNT=integer:0'
run_eval no-smm/enable-high \
	'set N \_SB.PCI0.TVAL 0; execute \_SB.PCI0.RP01.RTD3._ON; execute \_SB.PCI0.EVTS; execute \_SB.PCI0.PCNT; execute \_SB.PCI0.DCNT' \
	'\_SB.PCI0.RP01.RTD3._ON=none' '\_SB.PCI0.EVTS=integer:0x1516' \
	'\_SB.PCI0.PCNT=integer:0' '\_SB.PCI0.DCNT=integer:2'

# Bluetooth reset without optional CNVi GPIOs preserves BTRK(0), delay,
# BTRK(1), delay. With the optional GPIOs, their disable operations precede
# BTRK(0) and their enable operations precede BTRK(1), matching the baseline.
run_eval enable-high \
	'execute \_SB.PCI0.RP01.BT00.BTRT._RST; execute \_SB.PCI0.EVTS; execute \_SB.PCI0.PCNT' \
	'\_SB.PCI0.RP01.BT00.BTRT._RST=none' '\_SB.PCI0.EVTS=integer:0x1716' \
	'\_SB.PCI0.PCNT=integer:2'
run_eval bt-optional \
	'execute \_SB.PCI0.RP01.BT00.BTRT._RST; execute \_SB.PCI0.EVTS; execute \_SB.PCI0.PCNT' \
	'\_SB.PCI0.RP01.BT00.BTRT._RST=none' '\_SB.PCI0.EVTS=integer:0x1a1c171b1d16' \
	'\_SB.PCI0.PCNT=integer:6'
run_eval no-smm/bt-optional \
	'execute \_SB.PCI0.RP01.BT00.BTRT._RST; execute \_SB.PCI0.EVTS; execute \_SB.PCI0.DCNT' \
	'\_SB.PCI0.RP01.BT00.BTRT._RST=none' '\_SB.PCI0.EVTS=integer:0x1a1c171b1d16' \
	'\_SB.PCI0.DCNT=integer:6'
