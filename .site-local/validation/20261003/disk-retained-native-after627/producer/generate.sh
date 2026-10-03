#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/normal-efi-disk-producers-after625.ZBl2DT
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
tools="$producer/util/efi_capsule"
trust=/home/sean/q35-capsule-native-signing.PuAsza/trust.pem
target="$stage/targetA/build/coreboot.rom"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
test ! -e "$stage/disk-a.cap"
for image in initial9 targetA; do
  sha256sum -c "$stage/$image/outputs.sha256"
done
sha256sum "$stage/initial9/build/coreboot.rom" "$target" "$trust" \
  "$tools/generate_capsule.py" "$tools/validate_capsule.py" "$stage/generate.sh" \
  > "$stage/generation-before.sha256"
python3 "$tools/generate_capsule.py" "$target" --output "$stage/disk-a.cap" \
  --guid 00112233-4455-6677-8899-aabbccddeeff --fw-version 0x001a000a \
  --lsv 0x001a0009 --initiate-reset \
  --signer-private-cert /home/sean/q35-capsule-native-signing.PuAsza/combined-signer.pem \
  --trusted-public-cert "$trust"
python3 "$tools/validate_capsule.py" --capsule "$stage/disk-a.cap" \
  --guid 00112233-4455-6677-8899-aabbccddeeff --embedded-drivers 0 \
  --fw-version 0x001a000a --lsv 0x001a0009 --image "$target" --region COREBOOT \
  --trusted-public-cert "$trust" --initiate-reset --require-fmap
sha256sum -c "$stage/generation-before.sha256"
for image in initial9 targetA; do
  sha256sum -c "$stage/$image/outputs.sha256"
done
sha256sum "$stage/disk-a.cap" > "$stage/capsule.sha256"
