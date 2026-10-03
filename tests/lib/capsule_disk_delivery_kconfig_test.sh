#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
selected=$(realpath "$1")
temporary=$(mktemp -d)
trap 'if [ "${KEEP_CAPSULE_CONFIG_TMP:-0}" = 1 ]; then printf "%s\n" "$temporary"; \
	else rm -rf "$temporary"; fi' EXIT HUP INT TERM

sha256sum "$root/src/Kconfig" "$root/src/drivers/efi/Kconfig" \
	"$root/src/drivers/efi/capsule_delivery_policy.c" "$selected" \
	"$root/configs/config.emulation_qemu_x86_q35_smm_tseg" \
	"$root/tests/lib/capsule_disk_delivery_kconfig_test.sh" > "$temporary/inputs.sha256"

resolve()
{
	name=$1
	input=$2
	kconfig=$3
	shift 3
	mkdir -p "$temporary/build-$name"
	cp "$input" "$temporary/input-$name"
	printf '%s\n' "$@" >> "$temporary/input-$name"
	cp "$temporary/input-$name" "$temporary/config-$name"
	make -C "$root" -s UPDATED_SUBMODULES=1 obj="$temporary/build-$name" \
		DOTCONFIG="$temporary/config-$name" KBUILD_KCONFIG="$kconfig" \
		olddefconfig > "$temporary/resolve-$name.log" 2>&1
}

enabled()
{
	grep -qx "CONFIG_$2=y" "$temporary/config-$1"
}

disabled()
{
	! grep -qx "CONFIG_$2=y" "$temporary/config-$1"
}

# The caller supplies an actual selected typed-broker configuration. Do not
# replace platform selection with a fabricated Kconfig or generated header.
resolve typed "$selected" "$root/src/Kconfig" \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
for symbol in CAPSULE_BROKER_CONTRACT DRIVERS_EFI_VARIABLE_STORE \
	DRIVERS_EFI_FW_INFO DRIVERS_EFI_CAPSULE_DELIVERY_POLICY \
	DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT; do
	enabled typed "$symbol"
done
for symbol in DRIVERS_EFI_UPDATE_CAPSULES SMMSTORE SMMSTORE_FULL_FLASH_ACCESS; do
	disabled typed "$symbol"
done

resolve disk-disabled "$selected" "$root/src/Kconfig" \
	'# CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT is not set'
disabled disk-disabled DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
resolve no-public "$selected" "$root/src/Kconfig" \
	'# CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY is not set' \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
disabled no-public DRIVERS_EFI_CAPSULE_DELIVERY_POLICY
disabled no-public DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
disabled no-public DRIVERS_EFI_UPDATE_CAPSULES

# Test prerequisites on a genuine board configuration without a broker
# selecting them indirectly. The legacy updater remains an alternative.
board="$root/configs/config.emulation_qemu_x86_q35_smm_tseg"
resolve public "$board" "$root/src/Kconfig" \
	CONFIG_DRIVERS_EFI_VARIABLE_STORE=y CONFIG_DRIVERS_EFI_FW_INFO=y \
	CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY=y \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
enabled public DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
disabled public DRIVERS_EFI_UPDATE_CAPSULES
disabled public SMMSTORE
resolve missing-store "$board" "$root/src/Kconfig" \
	'# CONFIG_DRIVERS_EFI_VARIABLE_STORE is not set' CONFIG_DRIVERS_EFI_FW_INFO=y \
	CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY=y \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
disabled missing-store DRIVERS_EFI_VARIABLE_STORE
disabled missing-store DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
resolve missing-info "$board" "$root/src/Kconfig" \
	CONFIG_DRIVERS_EFI_VARIABLE_STORE=y '# CONFIG_DRIVERS_EFI_FW_INFO is not set' \
	CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY=y \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
disabled missing-info DRIVERS_EFI_FW_INFO
disabled missing-info DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
resolve no-producer "$board" "$root/src/Kconfig" \
	CONFIG_DRIVERS_EFI_VARIABLE_STORE=y CONFIG_DRIVERS_EFI_FW_INFO=y \
	'# CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY is not set' \
	'# CONFIG_DRIVERS_EFI_UPDATE_CAPSULES is not set' \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
disabled no-producer DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
resolve legacy "$board" "$root/src/Kconfig" \
	CONFIG_DRIVERS_EFI_VARIABLE_STORE=y CONFIG_DRIVERS_EFI_FW_INFO=y \
	CONFIG_SMMSTORE=y CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y \
	'# CONFIG_DRIVERS_EFI_CAPSULE_DELIVERY_POLICY is not set' \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
enabled legacy DRIVERS_EFI_UPDATE_CAPSULES
enabled legacy DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT
disabled legacy DRIVERS_EFI_CAPSULE_DELIVERY_POLICY

# Restore only the previous disk dependency. Resolve the real full tree,
# substituting this one EFI Kconfig, rather than modeling option visibility.
guard='depends on DRIVERS_EFI_UPDATE_CAPSULES || DRIVERS_EFI_CAPSULE_DELIVERY_POLICY'
test "$(grep -Fc "$guard" "$root/src/drivers/efi/Kconfig")" -eq 1
sed "s/$guard/depends on DRIVERS_EFI_UPDATE_CAPSULES/" \
	"$root/src/drivers/efi/Kconfig" > "$temporary/legacy-dependency-Kconfig"
sed 's/depends on DRIVERS_EFI_UPDATE_CAPSULES$/depends on DRIVERS_EFI_UPDATE_CAPSULES || DRIVERS_EFI_CAPSULE_DELIVERY_POLICY/' \
	"$temporary/legacy-dependency-Kconfig" > "$temporary/inverse-Kconfig"
cmp "$root/src/drivers/efi/Kconfig" "$temporary/inverse-Kconfig"
for source in "$root"/src/drivers/*/Kconfig; do
	if [ "$source" = "$root/src/drivers/efi/Kconfig" ]; then
		printf 'source "%s"\n' "$temporary/legacy-dependency-Kconfig"
	else
		printf 'source "%s"\n' "$source"
	fi
done > "$temporary/driver-first-level-Kconfig"
sed "s|source \"src/drivers/\*/Kconfig\"|source \"$temporary/driver-first-level-Kconfig\"|" \
	"$root/src/Kconfig" > "$temporary/legacy-tree-Kconfig"
resolve restored-dependency "$selected" "$temporary/legacy-tree-Kconfig" \
	CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=y
enabled restored-dependency DRIVERS_EFI_CAPSULE_DELIVERY_POLICY
disabled restored-dependency DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT

sha256sum -c "$temporary/inputs.sha256"
printf '%s\n' 'capsule disk delivery Kconfig tests PASS'
