#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
make_command=${MAKE:-make}
makefile="$root/src/drivers/smmstore/Makefile.mk"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Early UEFI-backed options retain a read-only SMMSTORE lookup.  The writable
# backend calls boot_device_rw(), so only ramstage and SMM may link store.c.
! grep -Eq '^all-.*store\.c' "$makefile"
grep -qx 'bootblock-$(CONFIG_SMMSTORE) += lookup.c' "$makefile"
grep -qx 'romstage-$(CONFIG_SMMSTORE) += lookup.c' "$makefile"
grep -qx 'ramstage-$(CONFIG_SMMSTORE) += store.c ramstage.c' "$makefile"
grep -qx 'smm-$(CONFIG_SMMSTORE) += store.c smi.c' "$makefile"

cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" "$tmp/q35"
cat >> "$tmp/q35" <<'EOF'
CONFIG_SMMSTORE=y
CONFIG_DRIVERS_EFI_VARIABLE_STORE=y
CONFIG_USE_UEFI_VARIABLE_STORE=y
CONFIG_DRIVERS_EFI_FW_INFO=y
CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y
EOF
"$make_command" -s -C "$root" DOTCONFIG="$tmp/q35" \
	BUILD_DIR="$tmp/build" olddefconfig
for symbol in SMMSTORE DRIVERS_EFI_VARIABLE_STORE USE_UEFI_VARIABLE_STORE \
	DRIVERS_EFI_FW_INFO \
	DRIVERS_EFI_UPDATE_CAPSULES; do
	grep -qx "CONFIG_${symbol}=y" "$tmp/q35"
done

# Link the configured image so unresolved per-stage ownership is observable.
"$make_command" -s -C "$root" DOTCONFIG="$tmp/q35" \
	BUILD_DIR="$tmp/build" -j2
