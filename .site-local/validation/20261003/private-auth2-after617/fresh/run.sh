#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

stage=/home/sean/native-private-auth2-final-retry.mPv628
source=/home/sean/Documents/.cdk2-worktrees/native-private-auth2-after616
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
export COREBOOT_TREE=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
export COREBOOT_CONFIG=/home/sean/disk-capsule-producer.5FwfSp/initial9-retry/full.config
export COREBOOT_ROM=/home/sean/disk-capsule-producer.5FwfSp/initial9-retry/build/coreboot.rom
export CBFSTOOL=/home/sean/disk-capsule-producer.5FwfSp/initial9-retry/build/util/cbfstool/cbfstool
export COREBOOT_ABSENT_ROM=/home/sean/q35-linear-no-endpoint-fwinfo.19jrz0/build/coreboot.rom
export CBFSTOOL_ABSENT=/home/sean/q35-linear-no-endpoint-fwinfo.19jrz0/build/util/cbfstool/cbfstool
export COREBOOT_OLD_GENERAL_ROM=/home/sean/q35-public-linear-fwinfo.L5TEX2/build/coreboot.rom
export CBFSTOOL_OLD_GENERAL=/home/sean/q35-public-linear-fwinfo.L5TEX2/build/util/cbfstool/cbfstool
ulimit -s 65536
ulimit -c 0

git -C "$source" rev-parse HEAD > "$stage/consumer-base.txt"
git -C "$COREBOOT_TREE" rev-parse HEAD > "$stage/producer-head.txt"
git -C "$source/3rdparty/bearssl" rev-parse HEAD > "$stage/bearssl-head.txt"
sha256sum "$source/src/boot/Makefile" \
	"$source/tests/protected_variable_fullgraph_runtime_app.c" \
	"$source/tests/protected_variable_fullgraph_enrolled_inputs.sh" \
	"$source/tests/protected_variable_fullgraph_production_test.sh" \
	"$source/tests/protected_variable_private_media_check.c" \
	"$source/tests/protected_variable_private_media_check.sh" > "$stage/source-before.sha256"
sha256sum "$COREBOOT_CONFIG" "$COREBOOT_ROM" "$CBFSTOOL" \
	"$COREBOOT_ABSENT_ROM" "$CBFSTOOL_ABSENT" "$COREBOOT_OLD_GENERAL_ROM" \
	"$CBFSTOOL_OLD_GENERAL" "$stage/run.sh" > "$stage/inputs-before.sha256"

# Record the actual complete dependency closure, including the producer codec
# headers and HOST headers. Preprocessing creates no service or guest state.
codec_sources=()
for implementation in fv store record ftw certdb; do
	codec_sources+=("$COREBOOT_TREE/src/lib/payload_mm_authvar_$implementation.c")
done
cc -std=gnu11 -M -DENV_TEST=1 \
	-Du8=uint8_t -Du16=uint16_t -Du32=uint32_t -Du64=uint64_t \
	-Ds32=int32_t -Ds64=int64_t \
	-include "$COREBOOT_TREE/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$COREBOOT_TREE/src/commonlib/include" -I"$COREBOOT_TREE/src/commonlib/bsd/include" \
	-idirafter "$COREBOOT_TREE/src/include" "$source/tests/protected_variable_private_media_check.c" \
	"${codec_sources[@]}" > "$stage/codec-dependencies.make"
perl -0777 -ne 's/\\\n/ /g; for (split /\n/) { s/^[^:]+: //; print join("\n", split /\s+/), "\n"; }' \
	"$stage/codec-dependencies.make" | awk 'NF' | sort -u > "$stage/codec-dependencies.txt"
xargs -d '\n' sha256sum < "$stage/codec-dependencies.txt" > "$stage/codec-closure-before.sha256"

status=0
/usr/bin/time -o "$stage/native.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	bash "$source/tests/protected_variable_fullgraph_production_test.sh" "$stage/artifacts" \
	> "$stage/native.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$stage/outer.status"
sha256sum -c "$stage/source-before.sha256" > "$stage/source-after-check.log"
sha256sum -c "$stage/inputs-before.sha256" > "$stage/inputs-after-check.log"
sha256sum -c "$stage/codec-closure-before.sha256" > "$stage/codec-after-check.log"
git -C "$source" status --short > "$stage/source-after.status"
printf 'ACTUAL_OUTER_STATUS=%s\n' "$status"
cat "$stage/native.time"
exit "$status"
