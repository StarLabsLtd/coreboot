#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=/home/sean/Documents/.cdk2-worktrees/disk-capsule-denial-native-after612
producer=/home/sean/disk-capsule-producer.5FwfSp
core=/home/sean/disk-capsule-closed-core.E7NMM4
cd "$root"
/usr/bin/time -o "$producer/native.time" -p python3 tests/system_fmp_disk_denial_native_test.py \
	--initial "$producer/initial9-retry/build/coreboot.rom" \
	--target "$producer/targetA-retry/build/coreboot.rom" \
	--capsule "$producer/disk-a.cap" \
	--trust /home/sean/q35-capsule-native-signing.PuAsza/trust.pem \
	--capsule-tools /home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388/util/efi_capsule \
	--cbfstool "$producer/initial9-retry/build/util/cbfstool/cbfstool" \
	--core "$core/native/cdk2-coreboot-image.elf" --config "$core/resolved.config" \
	--header "$core/include/cdk2/config.h" \
	--inventory "$core/native/native-direct-composition-inventory.tsv" \
	--producer-config "$producer/initial9-retry/full.config" \
	--disk "$producer/baseline-nvme.raw" --output "$producer/run-1" \
	> "$producer/native.log" 2>&1
