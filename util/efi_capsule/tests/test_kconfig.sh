#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

{
	printf '%s\n' 'mainmenu "capsule transport test"'
	for symbol in PAYLOAD_EDK2 SMMSTORE CAPSULE_BROKER_ENDPOINT_PUBLICATION \
		CAPSULE_PLATFORM_ADAPTERS PAYLOAD_MM_CMS_VERIFY \
		PAYLOAD_MM_FMP_OWNER_AUTHVAR_BOOT; do
		printf 'config %s\n\tbool "test %s"\n' "$symbol" "$symbol"
	done
	printf 'source "%s/src/drivers/efi/Kconfig"\n' "$root"
	cat <<'EOF'

config TEST_TYPED_PROFILE
	bool "test typed profile"

config DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT
	default y if TEST_TYPED_PROFILE
EOF
} > "$temporary/Kconfig"

resolve()
{
	name=$1
	shift
	mkdir -p "$temporary/build-$name"
	printf '%s\n' "$@" > "$temporary/config-$name"
	make -C "$root" -s obj="$temporary/build-$name" \
		KBUILD_KCONFIG="$temporary/Kconfig" \
		DOTCONFIG="$temporary/config-$name" olddefconfig
}

resolve legacy \
	'CONFIG_PAYLOAD_EDK2=y' \
	'CONFIG_DRIVERS_EFI_VARIABLE_STORE=y' \
	'CONFIG_SMMSTORE=y' \
	'CONFIG_DRIVERS_EFI_FW_INFO=y' \
	'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y' \
	'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y'
grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=y' \
	"$temporary/config-legacy"
grep -qx 'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y' "$temporary/config-legacy"
grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_REGIONS="COREBOOT"' \
	"$temporary/config-legacy"
! grep -q '^CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=y$' \
	"$temporary/config-legacy"

resolve legacy-without-updater \
	'CONFIG_PAYLOAD_EDK2=y' \
	'CONFIG_DRIVERS_EFI_FW_INFO=y' \
	'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y'
! grep -q '^CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=y$' \
	"$temporary/config-legacy-without-updater"
! grep -q '^CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y$' \
	"$temporary/config-legacy-without-updater"

typed_prerequisites='CONFIG_CAPSULE_BROKER_ENDPOINT_PUBLICATION=y
CONFIG_CAPSULE_PLATFORM_ADAPTERS=y
CONFIG_PAYLOAD_MM_CMS_VERIFY=y
CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR_BOOT=y'
resolve typed \
	'CONFIG_TEST_TYPED_PROFILE=y' \
	'CONFIG_DRIVERS_EFI_FW_INFO=y' \
	$typed_prerequisites \
	'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y'
grep -qx 'CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=y' "$temporary/config-typed"
grep -qx 'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y' "$temporary/config-typed"
! grep -q '^CONFIG_DRIVERS_EFI_CAPSULE_REGIONS=' "$temporary/config-typed"
! grep -q '^CONFIG_DRIVERS_EFI_CAPSULE_INITIATE_RESET=y$' "$temporary/config-typed"

resolve typed-without-cms \
	'CONFIG_TEST_TYPED_PROFILE=y' \
	'CONFIG_DRIVERS_EFI_FW_INFO=y' \
	'CONFIG_CAPSULE_BROKER_ENDPOINT_PUBLICATION=y' \
	'CONFIG_CAPSULE_PLATFORM_ADAPTERS=y' \
	'CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR_BOOT=y' \
	'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y'
! grep -q '^CONFIG_DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT=y$' \
	"$temporary/config-typed-without-cms"
! grep -q '^CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y$' \
	"$temporary/config-typed-without-cms"

# Keep the checked-in legacy AMD capsule profile tied to EDK2.  Without the
# payload selection, its embedded-driver settings silently disappear while
# resolving the defconfig.
mkdir -p "$temporary/build-amd-embedded"
make -C "$root" -s obj="$temporary/build-amd-embedded" \
	DOTCONFIG="$temporary/config-amd-embedded" \
	KBUILD_DEFCONFIG="$root/configs/config.amd_birmanplus_glinda.rom_armor3_uefi_capsule_x86_64" \
	CONFIG_ANY_TOOLCHAIN=y defconfig
for setting in \
	CONFIG_PAYLOAD_EDK2=y \
	CONFIG_SMMSTORE=y \
	CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y \
	CONFIG_DRIVERS_EFI_CAPSULE_ACCEPT_EMBEDDED_DRIVERS=y \
	CONFIG_DRIVERS_EFI_CAPSULE_LEGACY_TRANSPORT=y \
	CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y \
	CONFIG_DRIVERS_EFI_CAPSULE_EMBED_FMP_DXE=y; do
	grep -qx "$setting" "$temporary/config-amd-embedded"
done

printf '%s\n' 'EFI capsule transport Kconfig matrix: PASS'
