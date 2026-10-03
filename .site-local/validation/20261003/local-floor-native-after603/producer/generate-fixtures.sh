#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
task_root=/home/sean/normal-local-floor-producer.uE8CdA
task_tools=/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386/util/efi_capsule
task_trust=/home/sean/q35-capsule-native-signing.PuAsza/trust.pem
task_signer=/home/sean/q35-capsule-native-signing.PuAsza/combined-signer.pem
task_target=$task_root/targetA/build/coreboot.rom
test ! -e "$task_root/reference-a.cap"
test ! -e "$task_root/below-floor-8.cap"
sha256sum "$task_root/initial9/build/coreboot.rom" "$task_target" \
	"$task_trust" "$task_tools/generate_capsule.py" \
	"$task_tools/validate_capsule.py" "$task_root/generate-fixtures.sh" \
	> "$task_root/generation-inputs-before.sha256"
python3 "$task_tools/generate_capsule.py" "$task_target" \
	--output "$task_root/reference-a.cap" \
	--guid 00112233-4455-6677-8899-aabbccddeeff --fw-version 0x001a000a \
	--lsv 0x001a0009 --initiate-reset --signer-private-cert "$task_signer" \
	--trusted-public-cert "$task_trust"
python3 "$task_tools/generate_capsule.py" "$task_target" \
	--output "$task_root/below-floor-8.cap" \
	--guid 00112233-4455-6677-8899-aabbccddeeff --fw-version 0x001a0008 \
	--lsv 0x001a0008 --initiate-reset --signer-private-cert "$task_signer" \
	--trusted-public-cert "$task_trust"
python3 "$task_tools/validate_capsule.py" --capsule "$task_root/reference-a.cap" \
	--guid 00112233-4455-6677-8899-aabbccddeeff --embedded-drivers 0 \
	--fw-version 0x001a000a --lsv 0x001a0009 --image "$task_target" \
	--region COREBOOT --trusted-public-cert "$task_trust" --require-fmap
python3 "$task_tools/validate_capsule.py" --capsule "$task_root/below-floor-8.cap" \
	--guid 00112233-4455-6677-8899-aabbccddeeff --embedded-drivers 0 \
	--fw-version 0x001a0008 --lsv 0x001a0008 --image "$task_target" \
	--region COREBOOT --trusted-public-cert "$task_trust" --require-fmap
sha256sum -c "$task_root/generation-inputs-before.sha256"
sha256sum "$task_root/reference-a.cap" "$task_root/below-floor-8.cap" \
	> "$task_root/fixtures.sha256"
