#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
makefile="$root/src/drivers/smmstore/Makefile.mk"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# The store backend calls boot_device_rw().  Only ramstage and SMM own a
# writable boot device and call this backend, so no earlier stage may link it.
! grep -Eq '^all-.*store\.c' "$makefile"
grep -qx 'ramstage-$(CONFIG_SMMSTORE) += store.c ramstage.c' "$makefile"
grep -qx 'smm-$(CONFIG_SMMSTORE) += store.c smi.c' "$makefile"

cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" "$tmp/q35"
cat >> "$tmp/q35" <<'EOF'
CONFIG_SMMSTORE=y
CONFIG_DRIVERS_EFI_VARIABLE_STORE=y
CONFIG_DRIVERS_EFI_FW_INFO=y
CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y
EOF
make -s -C "$root" DOTCONFIG="$tmp/q35" olddefconfig
for symbol in SMMSTORE DRIVERS_EFI_VARIABLE_STORE DRIVERS_EFI_FW_INFO \
	DRIVERS_EFI_UPDATE_CAPSULES; do
	grep -qx "CONFIG_${symbol}=y" "$tmp/q35"
done
