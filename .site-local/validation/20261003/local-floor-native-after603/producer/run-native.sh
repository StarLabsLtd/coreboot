#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
task_source=/home/sean/Documents/.cdk2-worktrees/local-floor-native-observer-after601
task_core=/home/sean/capsule-local-floor-final.irsKVf
task_root=/home/sean/normal-local-floor-producer.uE8CdA
task_producer=/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386
task_capsule="$task_root/below-floor-8.cap"
task_output="$task_root/below-floor"
test ! -e "$task_output"
mkdir "$task_output"
cd "$task_source"
test "$(git rev-parse HEAD)" = 35562cd077140b8678a81753803dbb7d5516b7f7
git rev-parse HEAD > "$task_output/source-head.txt"
git status --porcelain > "$task_output/source-status.txt"
test ! -s "$task_output/source-status.txt"
HOSTCC=/usr/bin/x86_64-linux-gnu-gcc-15 \
HOSTLD=/usr/bin/ld.bfd HOSTREADELF=/usr/bin/readelf \
NATIVE_PE_LINK="$task_core/native/cdk2-native-pe-link" \
NATIVE_PE_AUDIT="$task_core/native/cdk2-pe-exec-sections" \
NATIVE_PE_MARKER="$task_core/native/pe-relocation-marker.o" \
MM_FULL_CORE_CAPSULE="$task_capsule" \
MM_FULL_CORE_TARGET="$task_root/targetA/build/coreboot.rom" \
MM_FULL_CORE_PUBLIC_TRUST=/home/sean/q35-capsule-native-signing.PuAsza/trust.pem \
MM_FULL_CORE_ATTEMPT=0x001a0008 MM_FULL_CORE_LOCAL_FLOOR_REFUSAL=1 \
MM_FULL_CORE_CAPSULE_REPORT=1 \
MM_FULL_CORE_REFERENCE_CAPSULE="$task_root/reference-a.cap" \
CAPSULE_TOOL_ROOT="$task_producer/util/efi_capsule" \
sh tests/system_fmp_core_ram_stage_app_test.sh \
	"$task_core/include/cdk2/config.h" "$task_output/app" \
	> "$task_output/app.log" 2>&1
/usr/bin/time -o "$task_output/native.time" \
python3 tests/system_fmp_core_ram_native_test.py \
	--initial "$task_root/initial9/build/coreboot.rom" \
	--target "$task_root/targetA/build/coreboot.rom" \
	--capsule "$task_capsule" --trust /home/sean/q35-capsule-native-signing.PuAsza/trust.pem \
	--disk /home/sean/Documents/cdk2-validation/qemu/pr331-627e3bbe/qemu/fixtures/nvme-final-c4bec.raw \
	--app "$task_output/app/SystemFmpCoreRamStage.efi" --output "$task_output/run-1" \
	--config "$task_core/resolved.config" --header "$task_core/include/cdk2/config.h" \
	--core "$task_core/native/cdk2-coreboot-image.elf" \
	--inventory "$task_core/native/native-direct-image-inventory.tsv" \
	--capsule-tools "$task_producer/util/efi_capsule" \
	--cbfstool "$task_root/initial9/build/util/cbfstool/cbfstool" \
	--compiler /usr/bin/x86_64-linux-gnu-gcc-15 --linker /usr/bin/ld.bfd \
	--pe-link "$task_core/native/cdk2-native-pe-link" \
	--pe-audit "$task_core/native/cdk2-pe-exec-sections" \
	--pe-marker "$task_core/native/pe-relocation-marker.o" \
	--binding "$task_root/initial9/full.config" --binding "$task_root/targetA/full.config" \
	--binding /home/sean/normal-refusal-consumed-producer.VPNIdB/build-producer.sh \
	--binding "$task_root/generate-fixtures.sh" --binding "$task_root/validate-fixtures.sh" \
	--binding "$task_root/run-native.sh" \
	--expect-local-floor-refusal --attempt 0x001a0008 --expect-capsule-report \
	--reference-capsule "$task_root/reference-a.cap" \
	> "$task_output/native.log" 2>&1
