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

run_case protected 'Q35 capsule DMA: communication .* positive control passed' \
	-device intel-iommu,pt=off -device edu,dma_mask=0xffffffff
run_case injected 'Q35 capsule DMA: exact-range proof failed' \
	-device intel-iommu,pt=off -device edu,dma_mask=0xffffffff \
	-fw_cfg name=opt/q35/capsule-dma-fail,string=1
run_case missing_iommu 'Q35 DMA: VT-d lacks the required 48-bit address width' \
	-device edu,dma_mask=0xffffffff
run_case missing_requester \
	'Q35 DMA: expected exactly one segment-zero EDU requester' \
	-device intel-iommu,pt=off
run_case duplicate 'Q35 DMA: expected exactly one segment-zero EDU requester' \
	-device intel-iommu,pt=off \
	-device edu,dma_mask=0xffffffff -device edu,dma_mask=0xffffffff

rg -q 'Q35 DMA: denied EDU 0x18 in domain 1, reason 0x5, target unchanged, BME clear' \
	"$out/protected.serial"
rg -q 'Q35 DMA: default-deny active, .*EDU 0000:00:03.0' \
	"$out/protected.serial"
rg -q 'Q35 DMA: noncoherent page-walk table visibility established' \
	"$out/protected.serial"
rg -q 'Q35 DMA: handoff generation 1, domain 1 linked, six immutable pages' \
	"$out/protected.serial"
rg -q 'Q35 capsule DMA: communication .* and staging .* denied; positive control passed' \
	"$out/protected.serial"
rg -q 'Q35 VTD TAB .* 0x00006000' "$out/protected.serial"
rg -q 'DMA HANDOFF .* 0x0000009c' "$out/protected.serial"
rg -q 'vtd_iommu_translate: detected translation failure \(dev=00:03:00' \
	"$out/protected.qemu"
rg -q 'Q35 DMA: VT-d lacks the required 48-bit address width' \
	"$out/missing_iommu.serial"
rg -q 'Q35 DMA: expected exactly one segment-zero EDU requester' \
	"$out/missing_requester.serial"
rg -q 'Q35 DMA: expected exactly one segment-zero EDU requester' \
	"$out/duplicate.serial"
rg -q 'Q35 capsule DMA: exact-range proof failed' "$out/injected.serial"
if rg -q 'Q35 capsule DMA: communication .* positive control passed' \
	"$out/injected.serial"; then
	echo "injected Q35 capsule proof unexpectedly succeeded" >&2
	exit 1
fi
if rg -q 'Q35 DMA: handoff generation' "$out/missing_iommu.serial" \
	"$out/missing_requester.serial" "$out/duplicate.serial"; then
	echo "hostile Q35 case published a DMA handoff" >&2
	exit 1
fi
cmp "$rom" "$out/input.rom"
sha256sum "$out/input.rom" "$out"/*.serial "$out"/*.qemu >"$out/evidence.sha256"
sha256sum -c "$out/evidence.sha256"
echo "Q35 DMA protected and hostile QEMU cases: PASS ($out)"
