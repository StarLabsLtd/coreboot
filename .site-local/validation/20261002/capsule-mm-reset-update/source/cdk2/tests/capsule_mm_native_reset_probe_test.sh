#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
config=${1:?resolved config required}
build=${2:?isolated build required}
: "${NATIVE_PROBE_CFLAGS:?named Make gate required}"
: "${PROTECTED_ENTRY_CRYPTO_SOURCES:?named Make gate required}"
: "${PROTECTED_ENTRY_CRYPTO_FLAGS:?named Make gate required}"
: "${COREBOOT_ROM:?actual clean-reader ROM required}"
: "${CBFSTOOL:?matching tool required}"
mkdir -p "$build"
output=$(mktemp -d "$build/run.XXXXXX")
printf 'Native MM warm-reset artifacts: %s\n' "$output"
mkdir -p "$output/include/cdk2"
sed 's/^#define CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME .*/#define CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME 1/' \
	"$config" > "$output/include/cdk2/config.h"
sources="$root/tests/capsule_mm_native_reset_probe.c
$root/src/modules/dxe_core/core.c $root/src/modules/dxe_core/presence_lifecycle.c
$root/src/modules/dxe_core/database.c $root/src/modules/dxe_core/event.c
$root/src/modules/dxe_core/memory.c $root/src/modules/dxe_core/image.c
$root/src/modules/dxe_core/gcd.c $root/src/modules/security_stub/security_stub.c
$root/src/modules/variable_runtime/entry.c $root/src/modules/variable_runtime/model.c
$root/src/modules/variable_runtime/service.c $root/src/modules/variable_runtime/diagnostic.c
$root/src/modules/authvar_transport/image_policy.c $root/src/modules/authvar_transport/owner.c
$root/src/modules/authvar_transport/namespace.c $root/src/modules/authvar_transport/runtime.c
$root/src/modules/authvar_transport/transport.c $root/src/modules/authvar_transport/native_x86.c
$root/src/modules/reset_system/reset_system.c $root/src/modules/reset_system/diagnostic.c
$root/src/lib/payload_mm_authvar_service.c $root/src/lib/image_policy_snapshot.c
$root/src/lib/direct_image_table.c $root/src/lib/diagnostic.c $root/src/lib/mem.c
$root/src/boot/pe.c $root/src/boot/coreboot.c $root/src/boot/coreboot_checksum.c
$root/src/boot/coreboot_resource.c $root/src/boot/coreboot_hobs.c
${NATIVE_SERVICE_LIFECYCLE_SOURCES:-} $PROTECTED_ENTRY_CRYPTO_SOURCES"
objects=
index=0
if test "${MM_UPDATE_CAPSULE_ROUNDTRIP:-0}" = 1; then
	NATIVE_PROBE_CFLAGS="$NATIVE_PROBE_CFLAGS -DCDK2_MM_UPDATE_CAPSULE_ROUNDTRIP"
	sources="$sources $root/src/modules/capsule_runtime/entry.c
$root/src/modules/capsule_runtime/capsule_runtime.c
$root/src/modules/capsule_runtime/capsule_runtime_abi.c"
fi
for source in $sources; do
	index=$((index + 1))
	${HOSTCC:-cc} $NATIVE_PROBE_CFLAGS $PROTECTED_ENTRY_CRYPTO_FLAGS \
		-I"$output/include" -I"$root/include" -I"$root/src/boot" \
		-I"$root/src/modules/dxe_core" -c "$source" -o "$output/$index.o"
	objects="$objects $output/$index.o"
done
${HOSTCC:-cc} -m64 -c "$root/src/boot/entry32.S" -o "$output/entry32.o"
${LD:-ld} -m elf_x86_64 --gc-sections -T "$root/src/boot/cdk2.ld" \
	-e cdk2_coreboot_entry32 -Map "$output/probe.map" -o "$output/probe.elf" \
	"$output/entry32.o" $objects
cp "$COREBOOT_ROM" "$output/probe.rom"
"$CBFSTOOL" "$output/probe.rom" extract -n config -f "$output/coreboot.config"
for option in Q35_SMM_INVOCATION_NATIVE_PUBLIC_SERVICE_COMPONENT \
	DRIVERS_EFI_CAPSULE_MM_READ_ADMISSION DRIVERS_EFI_CAPSULE_RAM_HANDOFF; do
	grep -qx "CONFIG_$option=y" "$output/coreboot.config"
done
"$CBFSTOOL" "$output/probe.rom" remove -n fallback/payload
"$CBFSTOOL" "$output/probe.rom" add-payload -n fallback/payload -f "$output/probe.elf" -c lzma
status=0
qemu-img create -q -f raw "$output/nvme.raw" 16M
timeout 90 ${QEMU:-qemu-system-x86_64} -machine q35,smm=on,accel=tcg -m 1024M \
	-cpu max -smp 1 -nodefaults \
	-device intel-iommu,pt=off -device VGA,bus=pcie.0,addr=01 \
	-drive "if=none,id=nvme0,format=raw,file=$output/nvme.raw" \
	-device nvme,drive=nvme0,serial=CDK2MMRESET,bus=pcie.0,addr=03 \
	-device qemu-xhci,bus=pcie.0,addr=04 \
	-device edu,dma_mask=0xffffffff,bus=pcie.0,addr=05 \
	-global driver=cfi.pflash01,property=secure,value=on \
	-drive "if=pflash,format=raw,file=$output/probe.rom" \
	-display none -serial "file:$output/probe.serial.log" \
	-device isa-debug-exit,iobase=0xf4,iosize=0x04 > "$output/probe.qemu.log" 2>&1 || status=$?
if test "${MM_UPDATE_CAPSULE_ROUNDTRIP:-0}" = 1; then
	if test "$status" != 3 ||
	    test "$(grep -c '^CDK2_MM_RESET_BEGIN' "$output/probe.serial.log")" != 2 ||
	    test "$(grep -c '^CDK2_MM_UPDATE_ACTUAL_RAM_POLICY' "$output/probe.serial.log")" != 2 ||
	    test "$(grep -c '^CDK2_MM_UPDATE_ACTUAL_QUERY_PASS' "$output/probe.serial.log")" != 1 ||
	    test "$(grep -c '^CDK2_MM_UPDATE_REAL_UPDATE_CAPSULE_INITIATE_RESET' "$output/probe.serial.log")" != 1 ||
	    test "$(grep -c '^CDK2_MM_RESET_ACTUAL_CAPSULE_HOB_BYTES_PASS' "$output/probe.serial.log")" != 1 ||
	    test "$(grep -c '^CDK2_MM_UPDATE_ACTUAL_POPULATE_PASS' "$output/probe.serial.log")" != 1 ||
	    grep -q '^CDK2_MM_RESET_FAIL' "$output/probe.serial.log"; then
		tail -n 60 "$output/probe.serial.log" >&2
		cat "$output/probe.qemu.log" >&2
		exit 1
	fi
	printf '%s\n' 'Actual CapsuleRuntime Query/UpdateCapsule + initiated ResetSystem + next-boot population: PASS (opaque UX transport; no firmware writer claim)'
	exit 0
fi
if test "$status" != 3 || test "$(grep -c '^CDK2_MM_RESET_BEGIN' "$output/probe.serial.log")" != 2 ||
    test "$(grep -c '^CDK2_MM_RESET_REAL_RESET_SYSTEM_WARM' "$output/probe.serial.log")" != 1 ||
    test "$(grep -c '^CDK2_MM_RESET_ACTUAL_MM_POINTER_WRITTEN' "$output/probe.serial.log")" != 1 ||
    test "$(grep -c '^CDK2_MM_RESET_ACTUAL_CAPSULE_HOB_BYTES_PASS' "$output/probe.serial.log")" != 1 ||
    test "$(grep -c '^CDK2_MM_RESET_RAM_DELIVERY_REMAINS_OFF' "$output/probe.serial.log")" != 2 ||
    grep -q '^CDK2_MM_RESET_FAIL' "$output/probe.serial.log"; then
	tail -n 60 "$output/probe.serial.log" >&2
	cat "$output/probe.qemu.log" >&2
	exit 1
fi
printf '%s\n' 'Actual MM pointer + ResetSystem warm reset + canonical capsule HOB bytes: PASS (RAM capability remains off)'
