#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 coreboot-rom output-directory" >&2
	exit 2
fi

rom=$(realpath "$1")
out=$2
test -f "$rom"
mkdir "$out"
cp --reflink=auto "$rom" "$out/input.rom"
chmod a-w "$out/input.rom"
truncate -s 1048576 "$out/nvme.raw"
truncate -s 1048576 "$out/nvme1.raw"
qemu_pid=
trap 'if [ -n "$qemu_pid" ]; then kill "$qemu_pid" 2>/dev/null || true; fi' \
	EXIT HUP INT TERM

run_case()
{
	name=$1
	marker=$2
	shift 2
	qemu-system-x86_64 \
		-machine pc-q35-10.1,smm=on,accel=tcg -m 1024M \
		-nographic -monitor none -no-reboot -bios "$out/input.rom" \
		-serial file:"$out/$name.serial" \
		-debugcon file:"$out/$name.debug" \
		-global isa-debugcon.iobase=0x402 "$@" \
		>"$out/$name.qemu" 2>&1 &
	qemu_pid=$!
	for tick in $(seq 1 60); do
		if test -f "$out/$name.serial" &&
			rg -q "$marker" \
				"$out/$name.serial"; then
			break
		fi
		sleep 0.25
	done
	kill "$qemu_pid" 2>/dev/null || true
	wait "$qemu_pid" 2>/dev/null || true
	qemu_pid=
}

run_case protected 'Q35 DMA: handoff generation .* domains 1/2 linked' \
	-device intel-iommu,pt=off \
	-drive if=none,id=nvme0,format=raw,file="$out/nvme.raw" \
	-device nvme,drive=nvme0,serial=Q35DMA,bus=pcie.0,addr=03 \
	-device qemu-xhci,id=xhci,bus=pcie.0,addr=04 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05
run_case missing_iommu 'Q35 DMA: VT-d lacks the required 48-bit address width' \
	-drive if=none,id=nvme0,format=raw,file="$out/nvme.raw" \
	-device nvme,drive=nvme0,serial=Q35DMA,bus=pcie.0,addr=03 \
	-device qemu-xhci,id=xhci,bus=pcie.0,addr=04 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05
run_case missing_nvme 'Q35 DMA: expected exactly one NVMe requester' \
	-device intel-iommu,pt=off \
	-device qemu-xhci,id=xhci,bus=pcie.0,addr=04 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05
run_case missing_xhci 'Q35 DMA: expected exactly one XHCI requester' \
	-device intel-iommu,pt=off \
	-drive if=none,id=nvme0,format=raw,file="$out/nvme.raw" \
	-device nvme,drive=nvme0,serial=Q35DMA,bus=pcie.0,addr=03 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05
run_case missing_edu 'Q35 DMA: expected exactly one unlisted EDU' \
	-device intel-iommu,pt=off \
	-drive if=none,id=nvme0,format=raw,file="$out/nvme.raw" \
	-device nvme,drive=nvme0,serial=Q35DMA,bus=pcie.0,addr=03 \
	-device qemu-xhci,id=xhci,bus=pcie.0,addr=04
run_case duplicate_nvme 'Q35 DMA: expected exactly one NVMe requester' \
	-device intel-iommu,pt=off \
	-drive if=none,id=nvme0,format=raw,file="$out/nvme.raw" \
	-device nvme,drive=nvme0,serial=Q35DMA0,bus=pcie.0,addr=03 \
	-drive if=none,id=nvme1,format=raw,file="$out/nvme1.raw" \
	-device nvme,drive=nvme1,serial=Q35DMA1,bus=pcie.0,addr=06 \
	-device qemu-xhci,id=xhci,bus=pcie.0,addr=04 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05
run_case duplicate_xhci 'Q35 DMA: expected exactly one XHCI requester' \
	-device intel-iommu,pt=off \
	-drive if=none,id=nvme0,format=raw,file="$out/nvme.raw" \
	-device nvme,drive=nvme0,serial=Q35DMA,bus=pcie.0,addr=03 \
	-device qemu-xhci,id=xhci0,bus=pcie.0,addr=04 \
	-device qemu-xhci,id=xhci1,bus=pcie.0,addr=06 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05
run_case duplicate_edu 'Q35 DMA: expected exactly one unlisted EDU' \
	-device intel-iommu,pt=off \
	-drive if=none,id=nvme0,format=raw,file="$out/nvme.raw" \
	-device nvme,drive=nvme0,serial=Q35DMA,bus=pcie.0,addr=03 \
	-device qemu-xhci,id=xhci,bus=pcie.0,addr=04 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=06

rg -q 'Q35 DMA: denied unlisted EDU 0x28, reason 0x2, target unchanged, BME clear' \
	"$out/protected.serial"
rg -q 'Q35 DMA: default-deny active, .*NVMe 0000:00:03.0 32 pages .*XHCI 0000:00:04.0 128 pages .*EDU unlisted' \
	"$out/protected.serial"
rg -q 'Q35 DMA: noncoherent page-walk table visibility established' \
	"$out/protected.serial"
rg -q 'Q35 DMA: handoff generation 1, domains 1/2 linked, ten immutable table pages, 32/128 immutable arena pages' \
	"$out/protected.serial"
rg -q 'Q35 VTD TAB .* 0x0000a000' "$out/protected.serial"
rg -q 'Q35 DMA ARE .* 0x000a1000' "$out/protected.serial"
rg -q 'DMA HANDOFF .* 0x00000068' "$out/protected.serial"
rg -q 'vtd_iommu_translate: detected translation failure \(dev=00:05:00' \
	"$out/protected.qemu"
rg -q 'Q35 DMA: VT-d lacks the required 48-bit address width' \
	"$out/missing_iommu.serial"
rg -q 'Q35 DMA: expected exactly one NVMe requester' "$out/missing_nvme.serial"
rg -q 'Q35 DMA: expected exactly one XHCI requester' "$out/missing_xhci.serial"
rg -q 'Q35 DMA: expected exactly one unlisted EDU' "$out/missing_edu.serial"
rg -q 'Q35 DMA: expected exactly one NVMe requester' "$out/duplicate_nvme.serial"
rg -q 'Q35 DMA: expected exactly one XHCI requester' "$out/duplicate_xhci.serial"
rg -q 'Q35 DMA: expected exactly one unlisted EDU' "$out/duplicate_edu.serial"
if rg -q 'Q35 DMA: handoff generation' "$out/missing_iommu.serial" \
	"$out/missing_nvme.serial" "$out/missing_xhci.serial" \
	"$out/missing_edu.serial" "$out/duplicate_nvme.serial" \
	"$out/duplicate_xhci.serial" "$out/duplicate_edu.serial"; then
	echo "hostile Q35 case published a DMA handoff" >&2
	exit 1
fi
cmp "$rom" "$out/input.rom"
sha256sum "$out/input.rom" "$out"/*.serial "$out"/*.qemu >"$out/evidence.sha256"
sha256sum -c "$out/evidence.sha256"
echo "Q35 DMA real-requester protected and hostile QEMU cases: PASS ($out)"
