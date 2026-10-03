#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
root=/home/sean/Documents/.cdk2-worktrees/disk-no-request-native-after619
producer=/home/sean/disk-no-request-hex-producer-after619.pXSGcd
core=/home/sean/disk-request-normal-protected-core-after619.1Zg8yk
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
ulimit -c 0
sha256sum "$root/tests/system_fmp_disk_denial_native_test.py" \
  "$root/tests/system_fmp_disk_denial_runner_test.py" "$producer/run-native.sh" > "$producer/observer-before.sha256"
status=0
/usr/bin/time -o "$producer/native.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
  python3 "$root/tests/system_fmp_disk_denial_native_test.py" --expect-no-request \
  --initial "$producer/initial9/build/coreboot.rom" --target "$producer/targetA/build/coreboot.rom" \
  --capsule "$producer/disk-a.cap" --trust /home/sean/q35-capsule-native-signing.PuAsza/trust.pem \
  --capsule-tools /home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388/util/efi_capsule \
  --cbfstool "$producer/initial9/build/util/cbfstool/cbfstool" \
  --core "$core/native/cdk2-coreboot-image.elf" --config "$core/resolved.config" \
  --header "$core/include/cdk2/config.h" --inventory "$core/native/native-direct-composition-inventory.tsv" \
  --producer-config "$producer/initial9/full.config" \
  --disk /home/sean/disk-capsule-producer.5FwfSp/baseline-nvme.raw --output "$producer/run-1" \
  > "$producer/native.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$producer/native.status"
sha256sum -c "$producer/observer-before.sha256" > "$producer/observer-after-check.log"
cat "$producer/native.time"
exit "$status"
