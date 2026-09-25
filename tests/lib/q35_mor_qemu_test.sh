#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: $0 selected-coreboot-rom output-directory" >&2
	exit 2
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
rom=$(realpath "$1")
output=$2
test -f "$rom"
mkdir "$output"
output=$(realpath "$output")
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-mor-qemu.XXXXXX")
qemu_pid=
cleanup()
{
	if [ -n "$qemu_pid" ]; then
		kill "$qemu_pid" 2>/dev/null || true
		wait "$qemu_pid" 2>/dev/null || true
	fi
	rm -rf "$temporary"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$temporary/include"
touch "$temporary/include/config.h"
cat > "$temporary/include/types.h" <<'EOF'
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <commonlib/bsd/cb_err.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
EOF
${HOSTCC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
	-Wconversion -Wshadow -Wstrict-prototypes \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
	-D__TEST__ -D__COREBOOT__ -DCONFIG_DEFAULT_CONSOLE_LOGLEVEL=0 \
	-DCONFIG_SMMSTORE_BLOCK_SIZE=4096 -include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -idirafter "$root/src" \
	-idirafter "$root/src/include" \
	-idirafter "$root/src/commonlib/include" \
	-idirafter "$root/src/commonlib/bsd/include" \
	-idirafter "$root/src/arch/x86/include" \
	"$root/tests/lib/q35_mor_qemu_fixture.c" \
	"$root/src/lib/payload_mm_authvar_fv.c" \
	"$root/src/lib/payload_mm_authvar_ftw.c" \
	"$root/src/lib/payload_mm_authvar_store.c" \
	"$root/src/lib/payload_mm_authvar_record.c" \
	"$root/src/lib/payload_mm_authvar_mor_identity.c" \
	-o "$temporary/fixture"

cp --reflink=auto "$rom" "$output/selected.rom"
chmod a-w "$output/selected.rom"
ASAN_OPTIONS=detect_leaks=1 "$temporary/fixture" create \
	"$output/asserted.smmstore"
cp --reflink=auto "$output/selected.rom" "$output/mor.pflash"
chmod u+w "$output/mor.pflash"
dd if="$output/asserted.smmstore" of="$output/mor.pflash" \
	bs=65536 count=1 conv=notrunc status=none
cmp -i 65536 "$output/selected.rom" "$output/mor.pflash"
truncate -s 1048576 "$output/nvme.raw"

run_boot()
{
	name=$1
	qemu-system-x86_64 \
		-machine pc-q35-10.1,smm=on,accel=tcg -cpu max -m 1024M \
		-nographic -monitor none -no-reboot \
		-drive if=pflash,unit=0,format=raw,file="$output/mor.pflash" \
		-serial file:"$output/$name.serial" \
		-debugcon file:"$output/$name.debug" \
		-global isa-debugcon.iobase=0x402 \
		-device intel-iommu,pt=off \
		-drive if=none,id=nvme0,format=raw,file="$output/nvme.raw" \
		-device nvme,drive=nvme0,serial=Q35MOR,bus=pcie.0,addr=03 \
		-device qemu-xhci,id=xhci,bus=pcie.0,addr=04 \
		-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05 \
		>"$output/$name.qemu" 2>&1 &
	qemu_pid=$!
	complete=false
	for tick in $(seq 1 160); do
		if ! kill -0 "$qemu_pid" 2>/dev/null; then
			break
		fi
		if test -f "$output/$name.serial" &&
		   rg -q 'fatal linear boot failure|MOR linear boot cannot continue|Q35 MOR DMA: device init changed' \
			"$output/$name.serial"; then
			break
		fi
		if test -f "$output/$name.serial" &&
		   rg -q 'Q35 MOR DMA: empty-root deny switched to final protected root without disabling translation' \
			"$output/$name.serial" &&
		   rg -q 'Jumping to boot code at' "$output/$name.serial" &&
		   ASAN_OPTIONS=detect_leaks=1 "$temporary/fixture" cleared \
			"$output/mor.pflash" >/dev/null 2>&1; then
			complete=true
			break
		fi
		sleep 0.25
	done
	if [ "$complete" = true ]; then
		# Let the writable block backend settle before snapshotting.
		sleep 0.25
	fi
	kill "$qemu_pid" 2>/dev/null || true
	wait "$qemu_pid" 2>/dev/null || true
	qemu_pid=
	if [ "$complete" != true ]; then
		echo "Q35 MOR $name boot did not reach a cleared store and payload handoff" >&2
		rg -n 'Q35 MOR|Q35 DMA|fatal linear|MOR linear' \
			"$output/$name.serial" >&2 || true
		return 1
	fi
	ASAN_OPTIONS=detect_leaks=1 "$temporary/fixture" cleared \
		"$output/mor.pflash"
	rg -q 'Q35 MOR DMA: early empty-root deny active before device init' \
		"$output/$name.serial"
	rg -q 'Q35 MOR DMA: empty-root deny switched to final protected root without disabling translation' \
		"$output/$name.serial"
	rg -q 'Q35 DMA: denied unlisted EDU .* target unchanged, BME clear' \
		"$output/$name.serial"
	rg -q 'Jumping to boot code at' "$output/$name.serial"
	rg -q 'vtd_iommu_translate: detected translation failure \(dev=00:05:00' \
		"$output/$name.qemu"
	! rg -q 'fatal linear boot failure|MOR linear boot cannot continue|protected-root switch failed' \
		"$output/$name.serial"
}

run_boot first
sha256sum "$output/mor.pflash" > "$output/after-first.sha256"
cp --reflink=auto "$output/mor.pflash" "$temporary/after-first.pflash"
run_boot second
cmp "$temporary/after-first.pflash" "$output/mor.pflash"
cmp -i 65536 "$output/selected.rom" "$output/mor.pflash"

sha256sum "$output/selected.rom" "$output/asserted.smmstore" \
	"$output/mor.pflash" "$output"/*.serial "$output"/*.qemu \
	> "$output/evidence.sha256"
sha256sum -c "$output/evidence.sha256"
printf 'Q35 MOR selected-ROM two-boot writable-pflash validation: PASS (%s)\n' \
	"$output"
