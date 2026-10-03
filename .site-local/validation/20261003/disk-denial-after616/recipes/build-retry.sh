#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
output=/home/sean/disk-capsule-producer.5FwfSp
root=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
core=/home/sean/disk-capsule-closed-core.E7NMM4/native/cdk2-coreboot-image.elf
name=$1-retry
version=$2
mkdir -p "$output/$name"
sed -e "s|^CONFIG_PAYLOAD_FILE=.*|CONFIG_PAYLOAD_FILE=\"$core\"|" \
	-e "s/^CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=.*/CONFIG_DRIVERS_EFI_MAIN_FW_VERSION=$version/" \
	/home/sean/disk-capsule-closed-core.E7NMM4/producer.config > "$output/$name/full.config"
cp "$output/$name/full.config" "$output/$name/input.config"
git -C "$root" rev-parse HEAD > "$output/$name/source-head.txt"
git -C /home/sean/Documents/coreboot/3rdparty/mbedtls rev-parse HEAD \
	> "$output/$name/mbedtls-head.txt"
git -C /home/sean/Documents/coreboot/3rdparty/vboot rev-parse HEAD \
	> "$output/$name/vboot-head.txt"
sha256sum "$core" "$root/src/drivers/efi/Kconfig" \
	"$root/src/drivers/efi/capsule_delivery_policy.c" "$output/build-retry.sh" \
	"$output/$name/input.config" > "$output/$name/inputs-before.sha256"
cd "$root"
make -j2 UPDATED_SUBMODULES=1 obj="$output/$name/build" \
	DOTCONFIG="$output/$name/full.config" \
	VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot \
	olddefconfig > "$output/$name/configure.log" 2>&1
grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y' "$output/$name/full.config"
grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY=y' "$output/$name/full.config"
for symbol in DRIVERS_EFI_UPDATE_CAPSULES SMMSTORE SMMSTORE_FULL_FLASH_ACCESS; do
	! grep -qx "CONFIG_$symbol=y" "$output/$name/full.config"
done
sha256sum "$output/$name/full.config" > "$output/$name/resolved-before.sha256"
/usr/bin/time -o "$output/$name/build.time" -p make -j2 UPDATED_SUBMODULES=1 \
	obj="$output/$name/build" DOTCONFIG="$output/$name/full.config" \
	VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot \
	all > "$output/$name/build.log" 2>&1
sha256sum -c "$output/$name/inputs-before.sha256"
sha256sum -c "$output/$name/resolved-before.sha256"
sha256sum "$output/$name/build/coreboot.rom" \
	"$output/$name/build/util/cbfstool/cbfstool" > "$output/$name/output.sha256"
