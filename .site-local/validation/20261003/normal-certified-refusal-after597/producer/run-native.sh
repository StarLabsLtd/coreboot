#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

task_case=${1:?finite refusal case required}
task_source=/home/sean/Documents/.cdk2-worktrees/normal-refusal-continuation-after592
task_root=/home/sean/normal-refusal-consumed-producer.VPNIdB
task_core=/home/sean/capsule-linear-completion-normal.fw344b
task_producer=/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386
task_output="$task_root/$task_case"
case "$task_case" in
wrong-signer) task_capsule="$task_root/wrong-signer-a.cap" ;;
signed-byte) task_capsule="$task_root/signed-byte-corrupt-a.cap" ;;
*) exit 2 ;;
esac
test ! -e "$task_output"
mkdir "$task_output"
cd "$task_source"
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
MM_FULL_CORE_CERTIFIED_REFUSAL="$task_case" \
MM_FULL_CORE_REFERENCE_CAPSULE="$task_root/reference-a.cap" \
MM_FULL_CORE_SIGNER_CERT=/home/sean/normal-core-ram-refusal-fixtures.VxQuI9/wrong-signer.pem \
CAPSULE_TOOL_ROOT="$task_producer/util/efi_capsule" \
sh tests/system_fmp_core_ram_stage_app_test.sh \
	"$task_core/include/cdk2/config.h" "$task_output/app" \
	> "$task_output/app.log" 2>&1
set --
if test "$task_case" = wrong-signer; then
	set -- --signer-cert /home/sean/normal-core-ram-refusal-fixtures.VxQuI9/wrong-signer.pem
fi
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
	--binding "$task_root/build-producer.sh" --binding "$task_root/run-native.sh" \
	--expect-certified-refusal "$task_case" --reference-capsule "$task_root/reference-a.cap" \
	"$@" > "$task_output/native.log" 2>&1
