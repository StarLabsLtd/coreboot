#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

stage=/home/sean/pci-bus-mtrr-audit-after614.kFd3bH
source=/home/sean/Documents/.cdk2-worktrees/pci-bus-mtrr-displacement-after614
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
producer_config=/home/sean/disk-capsule-producer.5FwfSp/initial9-retry/full.config
bearssl=/home/sean/Documents/.cdk2-worktrees/fmp-owner-native-state-after-update/3rdparty/bearssl
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
export SOURCE_DATE_EPOCH=0
ulimit -s 65536
ulimit -c 0

git -C "$source" rev-parse HEAD > "$stage/source-base.txt"
git -C "$source" diff --binary > "$stage/source.patch"
git -C "$producer" rev-parse HEAD > "$stage/producer-head.txt"
git -C "$bearssl" rev-parse HEAD > "$stage/bearssl-head.txt"
source_hashes()
{
	while IFS= read -r -d '' path; do
		if test -f "$source/$path"; then
			sha256sum "$source/$path"
		fi
	done < <(git -C "$source" ls-files -z)
}
source_hashes > "$stage/source-before.sha256"
sha256sum "$stage/resolved.config" "$stage/run.sh" "$producer_config" \
	"$producer/src/drivers/efi/Kconfig" "$bearssl/inc/bearssl.h" \
	/usr/bin/gcc /usr/bin/objdump /usr/bin/objcopy \
	> "$stage/inputs-before.sha256"

status=0
/usr/bin/time -o "$stage/build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	make -C "$source" -j2 native-coreboot-image native-direct-mtrr-ownership-audit \
	CDK2_CONFIG="$stage/resolved.config" CDK2_BUILD_DIR="$stage/build" \
	COREBOOT_TREE="$producer" COREBOOT_CONFIG="$producer_config" \
	CDK2_BEARSSL_DIR="$bearssl" > "$stage/build.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$stage/outer.status"
source_hashes > "$stage/source-after.sha256"
cmp "$stage/source-before.sha256" "$stage/source-after.sha256" \
	> "$stage/source-comparison.log"
sha256sum -c "$stage/inputs-before.sha256" > "$stage/inputs-after-check.log"
git -C "$source" diff --binary > "$stage/source-after.patch"
cmp "$stage/source.patch" "$stage/source-after.patch" > "$stage/patch-comparison.log"
if test "$status" -eq 0; then
	sha256sum "$stage/build/native/cdk2-coreboot-image.elf" \
		"$stage/build/include/cdk2/config.h" \
		"$stage/build/native/direct-mtrr-ownership-inputs" \
		> "$stage/outputs.sha256"
fi
printf 'ACTUAL_OUTER_STATUS=%s\n' "$status"
cat "$stage/build.time"
exit "$status"
