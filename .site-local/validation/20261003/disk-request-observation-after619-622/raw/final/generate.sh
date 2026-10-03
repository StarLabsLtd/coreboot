#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
output=/home/sean/disk-no-request-hex-producer-after619.pXSGcd
tools=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388/util/efi_capsule
trust=/home/sean/q35-capsule-native-signing.PuAsza/trust.pem
target=$output/targetA/build/coreboot.rom
test ! -e "$output/disk-a.cap"
sha256sum "$output/initial9/build/coreboot.rom" "$target" "$trust" \
  "$tools/generate_capsule.py" "$tools/validate_capsule.py" "$output/generate.sh" > "$output/generation-before.sha256"
python3 "$tools/generate_capsule.py" "$target" --output "$output/disk-a.cap" \
  --guid 00112233-4455-6677-8899-aabbccddeeff --fw-version 0x001a000a \
  --lsv 0x001a0009 --initiate-reset \
  --signer-private-cert /home/sean/q35-capsule-native-signing.PuAsza/combined-signer.pem \
  --trusted-public-cert "$trust"
python3 "$tools/validate_capsule.py" --capsule "$output/disk-a.cap" \
  --guid 00112233-4455-6677-8899-aabbccddeeff --embedded-drivers 0 \
  --fw-version 0x001a000a --lsv 0x001a0009 --image "$target" --region COREBOOT \
  --trusted-public-cert "$trust" --initiate-reset --require-fmap
sha256sum -c "$output/generation-before.sha256"
sha256sum "$output/disk-a.cap" > "$output/capsule.sha256"
