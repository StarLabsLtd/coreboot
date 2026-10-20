#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

top="$1"
out="$2"
generator="$3"
mkdir -p "$out"

cc -E -P -x c \
	'-DCONFIG(option)=1' \
	-I "$top/src/soc/intel/common/block/include" \
	"$top/tests/acpi/smm-gpio.asl" -o "$out/smm-gpio.asl"
# Mock only the hardware SMI boundary; the methods remain the production ASL.
sed -i 's/APMC = 0xe8/GSMI ()/' "$out/smm-gpio.asl"
iasl -tc "$out/smm-gpio.asl" >/dev/null
SMM_GPIO_HELPER_AML="$out/smm-gpio-helper.aml" "$generator" >/dev/null

run_eval()
{
	name="$1"
	commands="$2"
	shift 2
	transcript="$out/$name.transcript"
	if ! timeout 15s acpiexec -b "$commands" "$out/smm-gpio.aml" \
		"$out/smm-gpio-helper.aml" >"$transcript" 2>&1; then
		cat "$transcript" >&2
		return 1
	fi
	if ! python3 "$top/tests/acpi/acpiexec-eval-check.py" "$transcript" "$@"; then
		cat "$transcript" >&2
		return 1
	fi
}

# Successful raw values remain Boolean through the production common helper.
run_eval raw-zero \
	'set N \_SB.PCI0.TSTS 0; set N \_SB.PCI0.TSVL 0; execute \_SB.PCI0.PGTX 1; execute \_SB.PCI0.TGHI; execute \_SB.PCI0.TGLO' \
	'\_SB.PCI0.PGTX=integer:0' '\_SB.PCI0.TGHI=integer:0' '\_SB.PCI0.TGLO=integer:1'
run_eval raw-one \
	'set N \_SB.PCI0.TSTS 0; set N \_SB.PCI0.TSVL 1; execute \_SB.PCI0.PGTX 1; execute \_SB.PCI0.TGHI; execute \_SB.PCI0.TGLO' \
	'\_SB.PCI0.PGTX=integer:1' '\_SB.PCI0.TGHI=integer:1' '\_SB.PCI0.TGLO=integer:0'

# A failed transaction retains its nonzero mailbox status but exposes raw low
# through the legacy Boolean API. Active-low normalization therefore yields one.
run_eval failed-transaction \
	'set N \_SB.PCI0.TSTS 1; set N \_SB.PCI0.TSVL 1; execute \_SB.PCI0.PGTX 1; execute \_SB.PCI0.GPST; execute \_SB.PCI0.GPVL; execute \_SB.PCI0.TGHI; execute \_SB.PCI0.TGLO; execute \_SB.PCI0.GPST' \
	'\_SB.PCI0.PGTX=integer:0' '\_SB.PCI0.GPST=integer:1' '\_SB.PCI0.GPVL=integer:1' \
	'\_SB.PCI0.TGHI=integer:0' '\_SB.PCI0.TGLO=integer:1' '\_SB.PCI0.GPST=integer:1'

# Request initialization prevents an absent service from exposing stale data.
run_eval no-service \
	'set N \_SB.PCI0.GPST 0; set N \_SB.PCI0.GPVL 1; set N \_SB.PCI0.TNRP 1; execute \_SB.PCI0.PGTX 1; execute \_SB.PCI0.GPST; execute \_SB.PCI0.GPVL' \
	'\_SB.PCI0.PGTX=integer:0' '\_SB.PCI0.GPST=integer:1' '\_SB.PCI0.GPVL=integer:0'

# A successful transport carrying a non-bit payload uses the same compatibility
# fallback without falsifying the successful mailbox status.
run_eval invalid-payload \
	'set N \_SB.PCI0.TSTS 0; set N \_SB.PCI0.TSVL 2; execute \_SB.PCI0.PGTX 1; execute \_SB.PCI0.GPST; execute \_SB.PCI0.GPVL; execute \_SB.PCI0.TGHI; execute \_SB.PCI0.TGLO' \
	'\_SB.PCI0.PGTX=integer:0' '\_SB.PCI0.GPST=integer:0' '\_SB.PCI0.GPVL=integer:2' \
	'\_SB.PCI0.TGHI=integer:0' '\_SB.PCI0.TGLO=integer:1'

# A later successful request is independent and proves the mailbox mutex remains usable.
run_eval failure-then-success \
	'set N \_SB.PCI0.TSTS 1; set N \_SB.PCI0.TSVL 1; execute \_SB.PCI0.PGTX 1; set N \_SB.PCI0.TSTS 0; execute \_SB.PCI0.PGTX 1; execute \_SB.PCI0.GPST' \
	'\_SB.PCI0.PGTX=integer:0' '\_SB.PCI0.PGTX=integer:1' '\_SB.PCI0.GPST=integer:0'

# Setter status remains unchanged and separate from the GET compatibility policy.
run_eval setter-status \
	'set N \_SB.PCI0.TSTS 1; execute \_SB.PCI0.PSTX 1; execute \_SB.PCI0.PCTX 1' \
	'\_SB.PCI0.PSTX=integer:1' '\_SB.PCI0.PCTX=integer:1'
