#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
make_command=${MAKE:-make}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cat > "$tmp/enabled" <<'EOF'
CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE=y
CONFIG_DRIVERS_EFI_MAIN_FW_GUID="11112233-4455-6677-8899-aabbccddeeff"
EOF
"$root/util/cdk2-config" "$tmp/enabled" "$tmp/profile"
grep -qx 'CONFIG_CDK2_PAYLOAD=y' "$tmp/profile"
grep -qx 'CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE=y' "$tmp/profile"
grep -qx 'CONFIG_CDK2_CAPSULE_MAIN_FW_GUID="11112233-4455-6677-8899-aabbccddeeff"' "$tmp/profile"

echo '# CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE is not set' > "$tmp/disabled"
"$root/util/cdk2-config" "$tmp/disabled" "$tmp/profile"
test "$(wc -l < "$tmp/profile")" -eq 1
grep -qx 'CONFIG_CDK2_PAYLOAD=y' "$tmp/profile"

echo 'CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE=y' > "$tmp/invalid"
if "$root/util/cdk2-config" "$tmp/invalid" "$tmp/profile" 2> "$tmp/error"; then
	echo 'missing GUID unexpectedly accepted' >&2
	exit 1
fi
grep -q 'requires CONFIG_DRIVERS_EFI_MAIN_FW_GUID' "$tmp/error"

cat > "$tmp/malformed" <<'EOF'
CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE=y
CONFIG_DRIVERS_EFI_MAIN_FW_GUID="not-a-guid"
EOF
if "$root/util/cdk2-config" "$tmp/malformed" "$tmp/profile" 2> "$tmp/error"; then
	echo 'malformed firmware GUID unexpectedly accepted' >&2
	exit 1
fi
grep -q 'requires a canonical firmware GUID' "$tmp/error"

cat > "$tmp/placeholder" <<'EOF'
CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE=y
CONFIG_DRIVERS_EFI_MAIN_FW_GUID="00112233-4455-6677-8899-aabbccddeeff"
EOF
if "$root/util/cdk2-config" "$tmp/placeholder" "$tmp/profile" 2> "$tmp/error"; then
	echo 'placeholder firmware GUID unexpectedly accepted' >&2
	exit 1
fi
grep -q 'requires a board-specific firmware GUID' "$tmp/error"

sed 's/00112233-4455-6677-8899-aabbccddeeff/00000000-0000-0000-0000-000000000000/' \
	"$tmp/placeholder" > "$tmp/nil-guid"
if "$root/util/cdk2-config" "$tmp/nil-guid" "$tmp/profile" 2> "$tmp/error"; then
	echo 'nil firmware GUID unexpectedly accepted' >&2
	exit 1
fi
grep -q 'requires a non-nil firmware GUID' "$tmp/error"

sed 's/00112233-4455-6677-8899-aabbccddeeff/ffffffff-ffff-ffff-ffff-ffffffffffff/' \
	"$tmp/placeholder" > "$tmp/max-guid"
if "$root/util/cdk2-config" "$tmp/max-guid" "$tmp/profile" 2> "$tmp/error"; then
	echo 'max firmware GUID unexpectedly accepted' >&2
	exit 1
fi
grep -q 'requires a non-max firmware GUID' "$tmp/error"

sed 's/aabbccddeeff/AABBCCDDEEFF/' "$tmp/placeholder" > "$tmp/placeholder-upper"
if "$root/util/cdk2-config" "$tmp/placeholder-upper" "$tmp/profile" 2> "$tmp/error"; then
	echo 'uppercase placeholder firmware GUID unexpectedly accepted' >&2
	exit 1
fi
grep -q 'requires a board-specific firmware GUID' "$tmp/error"

cp "$root/configs/config.starlabs_starbook_mtl" "$tmp/coreboot-enabled"
cat >> "$tmp/coreboot-enabled" <<'EOF'
CONFIG_PAYLOAD_CDK2=y
CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE=y
EOF
"$make_command" -s -C "$root" DOTCONFIG="$tmp/coreboot-enabled" \
	obj="$tmp/build-enabled" olddefconfig
for symbol in PAYLOAD_CDK2 PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE DRIVERS_EFI_VARIABLE_STORE \
	DRIVERS_EFI_FW_INFO DRIVERS_EFI_UPDATE_CAPSULES \
	DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT SMMSTORE; do
	grep -qx "CONFIG_${symbol}=y" "$tmp/coreboot-enabled"
done

cp "$root/configs/config.starlabs_starbook_mtl" "$tmp/coreboot-no-gfx"
cat >> "$tmp/coreboot-no-gfx" <<'EOF'
CONFIG_PAYLOAD_CDK2=y
CONFIG_NO_GFX_INIT=y
EOF
"$make_command" -s -C "$root" DOTCONFIG="$tmp/coreboot-no-gfx" \
	obj="$tmp/build-no-gfx" olddefconfig
if grep -qx 'CONFIG_NO_GFX_INIT=y' "$tmp/coreboot-no-gfx"; then
	echo 'CDK2 payload retained an unusable no-graphics configuration' >&2
	exit 1
fi

cp "$root/configs/config.starlabs_starbook_mtl" "$tmp/coreboot-disabled"
"$make_command" -s -C "$root" DOTCONFIG="$tmp/coreboot-disabled" \
	obj="$tmp/build-disabled" olddefconfig
if grep -qx 'CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE=y' \
	"$tmp/coreboot-disabled"; then
	echo 'CDK2 capsule profile unexpectedly enabled with EDK2' >&2
	exit 1
fi

cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" "$tmp/qemu-enabled"
cat >> "$tmp/qemu-enabled" <<'EOF'
CONFIG_PAYLOAD_CDK2=y
CONFIG_PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE=y
EOF
"$make_command" -s -C "$root" DOTCONFIG="$tmp/qemu-enabled" \
	obj="$tmp/build-qemu" olddefconfig \
	2> "$tmp/qemu-error"
test ! -s "$tmp/qemu-error"
for symbol in PAYLOAD_CDK2_CAPSULE_UPDATE_PROFILE DRIVERS_EFI_FW_INFO \
	DRIVERS_EFI_UPDATE_CAPSULES DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT; do
	grep -qx "CONFIG_${symbol}=y" "$tmp/qemu-enabled"
done

test "$(grep -c 'PcdCapsuleOnDiskSupport=TRUE' \
	"$root/payloads/external/edk2/Makefile")" -eq 1
test "$(sed -n '/ifeq ($(CONFIG_EDK2_FULL_SCREEN_SETUP),y)/,/endif/p' \
	"$root/payloads/external/edk2/Makefile" | sed -n \
	's/.*gEfiMdeModulePkgTokenSpaceGuid\.\(Pcd[^=]*ConOut[^=]*\)=.*/\1/p' | \
	sort -u | wc -l)" -eq 4
