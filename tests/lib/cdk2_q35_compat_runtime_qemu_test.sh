#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 coreboot-rom cbfstool" >&2
	exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
rom=$(realpath "$1")
cbfstool=$(realpath "$2")
qemu=${QEMU_SYSTEM_X86_64:-qemu-system-x86_64}
machine=${CDK2_Q35_MACHINE:-q35}
timeout=${CDK2_Q35_TIMEOUT:-180}
qemu_root=$root/payloads/external/cdk2/cdk2/util/qemu
fixture=$qemu_root/fixtures/nvme-final-c4bec.raw.xz
run=$(mktemp -d)
trap 'rm -rf "$run"' EXIT HUP INT TERM

test -f "$rom"
test -x "$cbfstool"
test -f "$fixture"
command -v "$qemu" >/dev/null
command -v xz >/dev/null

"$cbfstool" "$rom" extract -n config -f "$run/coreboot-config.txt" >/dev/null
"$cbfstool" "$rom" extract -n cdk2/config -f "$run/cdk2-config.txt" >/dev/null
"$qemu_root/bin/assert-coreboot-acceptance-config.sh" \
	"$run/coreboot-config.txt"
"$qemu_root/bin/assert-cdk2-rom-acceptance-config.sh" \
	"$run/cdk2-config.txt"
grep -qx 'CONFIG_CDK2_STRICT_DIRECT_RUNTIME=y' "$run/cdk2-config.txt"
grep -qx 'CONFIG_SMMSTORE=y' "$run/coreboot-config.txt"
grep -qx 'CONFIG_Q35_VTD_DMA_TEST_BACKEND=y' "$run/coreboot-config.txt"

console_row=$("$cbfstool" "$rom" layout | awk "/^'CONSOLE' / { print; exit }")
console_size=$(printf '%s\n' "$console_row" |
	sed -n 's/.*size \([0-9][0-9]*\), offset.*/\1/p')
console_offset=$(printf '%s\n' "$console_row" |
	sed -n 's/.*offset \([0-9][0-9]*\)).*/\1/p')
if [ "$console_offset" != 524288 ] || [ "$console_size" != 131072 ]; then
	echo "unexpected Q35 CONSOLE region: offset=$console_offset size=$console_size" >&2
	exit 1
fi

cp --reflink=auto "$rom" "$run/pflash-before.rom"
cp --reflink=auto "$rom" "$run/pflash.rom"
xz -dc "$fixture" >"$run/nvme.raw"
printf '%s  %s\n' \
	054a451e67b291adc7390301ef21c9df2de3582e768612cc66459e58d1305e36 \
	"$run/nvme.raw" | sha256sum -c - >/dev/null

# This fixed topology is part of the Q35 DMA-test contract.  It is a
# compatibility/runtime lane for legacy SMMSTORE, not production proof.
if ! "$qemu_root/bin/run-deadline.py" --kill-after 3 "$timeout" -- \
	"$qemu" -name cdk2-q35-compat-runtime \
	-machine "$machine",smm=on,accel=tcg -cpu qemu64 \
	-smp 4,sockets=1,cores=4,threads=1 -m 1024M \
	-rtc base=2026-01-01T00:00:00,clock=vm \
	-uuid 00112233-4455-6677-8899-aabbccddeeff -no-reboot -net none \
	-drive if=pflash,unit=0,format=raw,readonly=off,file="$run/pflash.rom" \
	-chardev file,id=ser,path="$run/serial.log" -serial chardev:ser \
	-debugcon file:"$run/debugcon.log" -global isa-debugcon.iobase=0x402 \
	-display none -device VGA,id=cdk2-vga,bus=pcie.0,addr=01 \
	-device intel-iommu,pt=off \
	-drive if=none,id=nvme0,format=raw,file="$run/nvme.raw",cache=unsafe \
	-device nvme,drive=nvme0,serial=CDK2ABNVME0001,bus=pcie.0,addr=03 \
	-device qemu-xhci,id=xhci,bus=pcie.0,addr=04 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05 \
	>"$run/qemu.log" 2>&1; then
	tail -n 80 "$run/serial.log" >&2 || true
	tail -n 40 "$run/qemu.log" >&2 || true
	exit 1
fi

printf '{"qemu_status":0}\n' >"$run/manifest.json"
dd if="$run/pflash-before.rom" of="$run/spi-console.before.bin" bs=1 \
	skip="$console_offset" count="$console_size" status=none
dd if="$run/pflash.rom" of="$run/spi-console.after.bin" bs=1 \
	skip="$console_offset" count="$console_size" status=none

grep -q 'Q35 DMA: denied unlisted EDU .* target unchanged, BME clear' \
	"$run/serial.log"
grep -q 'Q35 DMA: handoff generation 1, domains 1/2 linked' "$run/serial.log"
grep -q 'CDK2_LINUX_OK' "$run/serial.log"
grep -q 'CDK2_RUNTIME_OK' "$run/serial.log"
grep -q 'CDK2_EFIVAR_WRITTEN' "$run/serial.log"
grep -q 'ACPI: PM: Preparing to enter system sleep state S5' "$run/serial.log"
"$qemu_root/bin/assert-linear-run.py" nvme-linux "$run" \
	--output "$run/assertions.json"

printf '%s\n' 'CDK2 Q35 compatibility/runtime QEMU: PASS'
