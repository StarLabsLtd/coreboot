#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
task_root=/home/sean/normal-capsule-report-corrected-producer.xexfMA
task_tools=/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386/util/efi_capsule
task_trust=/home/sean/q35-capsule-native-signing.PuAsza/trust.pem
task_signer=/home/sean/q35-capsule-native-signing.PuAsza/combined-signer.pem
task_wrong=/home/sean/normal-core-ram-refusal-fixtures.VxQuI9
test ! -e "$task_root/reference-a.cap"
test ! -e "$task_root/wrong-signer-a.cap"
test ! -e "$task_root/signed-byte-corrupt-a.cap"
sha256sum "$task_root/initial9/build/coreboot.rom" \
	"$task_root/targetA/build/coreboot.rom" "$task_trust" \
	"$task_tools/generate_capsule.py" "$task_root/generate-fixtures.sh" \
	> "$task_root/generation-inputs-before.sha256"
python3 "$task_tools/generate_capsule.py" --output "$task_root/reference-a.cap" \
	--guid 00112233-4455-6677-8899-aabbccddeeff --fw-version 0x001a000a \
	--lsv 0x001a0009 --initiate-reset --signer-private-cert "$task_signer" \
	--trusted-public-cert "$task_trust" "$task_root/targetA/build/coreboot.rom"
python3 "$task_tools/generate_capsule.py" --output "$task_root/wrong-signer-a.cap" \
	--guid 00112233-4455-6677-8899-aabbccddeeff --fw-version 0x001a000a \
	--lsv 0x001a0009 --initiate-reset \
	--signer-private-cert "$task_wrong/wrong-combined-signer.pem" \
	--trusted-public-cert "$task_wrong/wrong-signer.pem" \
	"$task_root/targetA/build/coreboot.rom"
cp "$task_root/reference-a.cap" "$task_root/signed-byte-corrupt-a.cap"
perl -0777 -i -pe 'substr($_, -1, 1) = chr(ord(substr($_, -1, 1)) ^ 1)' \
	"$task_root/signed-byte-corrupt-a.cap"
sha256sum -c "$task_root/generation-inputs-before.sha256"
sha256sum "$task_root/reference-a.cap" "$task_root/wrong-signer-a.cap" \
	"$task_root/signed-byte-corrupt-a.cap" > "$task_root/fixtures.sha256"
