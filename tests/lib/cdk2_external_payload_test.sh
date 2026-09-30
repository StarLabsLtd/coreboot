#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

kconfig_name=$root/payloads/external/cdk2/Kconfig.name
kconfig=$root/payloads/external/cdk2/Kconfig
makefile=$root/payloads/external/Makefile.mk

cat > "$temporary/q35.defconfig" <<'EOF'
CONFIG_VENDOR_EMULATION=y
CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y
EOF
make -s -C "$root" DOTCONFIG="$temporary/off.config" \
	KBUILD_DEFCONFIG="$temporary/q35.defconfig" defconfig
grep -qx '# CONFIG_PAYLOAD_CDK2 is not set' "$temporary/off.config"

cat >> "$temporary/q35.defconfig" <<'EOF'
CONFIG_PAYLOAD_CDK2=y
EOF
make -s -C "$root" DOTCONFIG="$temporary/on.config" \
	KBUILD_DEFCONFIG="$temporary/q35.defconfig" defconfig
grep -qx 'CONFIG_PAYLOAD_CDK2=y' "$temporary/on.config"
grep -qx 'CONFIG_WANT_LINEAR_FRAMEBUFFER=y' "$temporary/on.config"
grep -qx 'CONFIG_PAYLOAD_OWNS_PCI_DEVICES=y' "$temporary/on.config"
grep -qx 'CONFIG_CDK2_NATIVE_STAGE=y' "$temporary/on.config"
grep -qx 'CONFIG_CDK2_LINEAR_BOOT=y' "$temporary/on.config"
grep -qx 'CONFIG_CDK2_COREBOOT_CAPSULE_PROFILE=y' "$temporary/on.config"
grep -qx 'CONFIG_CDK2_STRICT_DIRECT_RUNTIME=y' "$temporary/on.config"
grep -qx 'CONFIG_PAYLOAD_FILE="build/cdk2/native/cdk2-coreboot-image.elf"' \
	"$temporary/on.config"

# Selecting the payload must not silently select a variable-store backend.
! grep -qx 'CONFIG_SMMSTORE=y' "$temporary/on.config"
! grep -Eq 'select[[:space:]]+SMMSTORE' "$kconfig_name" "$kconfig"

grep -q 'coreboot-stage' "$makefile"
! grep -q 'native-coreboot-image' "$makefile"
grep -q 'submodule update --init --checkout --recursive' "$makefile"
grep -q 'payloads/external/cdk2/verify-source.sh $(CDK2_SOURCE)' "$makefile"
! grep -q "HEAD:\$(CDK2_SOURCE)" "$makefile"
grep -q 'COREBOOT_CONFIG="$(abspath $(DOTCONFIG))"' "$makefile"
grep -q 'COREBOOT_OUTPUT_DIR="$(abspath $(CDK2_OUTPUT))"' "$makefile"
grep -q 'LD="$(word 1,$(LD_x86_64))"' "$makefile"
grep -q 'READELF="$(READELF_x86_64)"' "$makefile"
grep -q "grep -qx 'CONFIG_CDK2_STRICT_DIRECT_RUNTIME=y'" "$makefile"
grep -q 'cdk2/coreboot-config-file := $(DOTCONFIG)' "$makefile"
grep -q 'cdk2/config-file := $(CDK2_CONFIG)' "$makefile"

# The outer integration admits only the source-built native route.
integration=$(sed -n '/^# cdk2$/,/^endif$/p' "$makefile")
! printf '%s\n' "$integration" | grep -Eq \
	'CDK2_PAYLOAD_FV|RETAINED_FV|SYSTEM_FMP_(FFS|PE)|compatibility-image'

"$root/tests/lib/cdk2_source_gitlink_test.sh"

printf '%s\n' 'CDK2 external payload integration: PASS'
