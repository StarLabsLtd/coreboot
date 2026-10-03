#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
task_source=/home/sean/Documents/.cdk2-worktrees/capsule-report-local-floor-cold-after607
task_core=/home/sean/capsule-local-floor-final.irsKVf
task_warm=/home/sean/normal-local-floor-producer.uE8CdA
task_output=/home/sean/capsule-report-local-floor-cold-retry.Aowjcu
task_producer=/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386
cd "$task_source"
test "$(git rev-parse HEAD)" = b4420149f4c534c884a79e383a25ed04108cc44e
test -z "$(git status --porcelain)"
git rev-parse HEAD > "$task_output/source-head.txt"
git status --porcelain > "$task_output/source-status.txt"
HOSTCC=/usr/bin/x86_64-linux-gnu-gcc-15 HOSTLD=/usr/bin/ld.bfd \
HOSTREADELF=/usr/bin/readelf NATIVE_PE_LINK="$task_core/native/cdk2-native-pe-link" \
NATIVE_PE_AUDIT="$task_core/native/cdk2-pe-exec-sections" \
NATIVE_PE_MARKER="$task_core/native/pe-relocation-marker.o" \
MM_COLD_CAPSULE_REPORT_LOCAL_FLOOR=1 \
/usr/bin/time -o "$task_output/app.time" \
sh tests/capsule_report_cold_app_test.sh "$task_core/include/cdk2/config.h" \
 "$task_warm/below-floor-8.cap" "$task_output/app" > "$task_output/app.log" 2>&1
/usr/bin/time -o "$task_output/native.time" \
python3 tests/capsule_report_cold_native_test.py \
 --warm "$task_warm/below-floor/run-1" --capsule "$task_warm/below-floor-8.cap" \
 --app "$task_output/app/CapsuleReportCold.efi" --output "$task_output/run-1" \
 --config "$task_core/resolved.config" --header "$task_core/include/cdk2/config.h" \
 --core "$task_core/native/cdk2-coreboot-image.elf" \
 --inventory "$task_core/native/native-direct-image-inventory.tsv" \
 --cbfstool "$task_warm/initial9/build/util/cbfstool/cbfstool" \
 --pe-audit "$task_core/native/cdk2-pe-exec-sections" \
 --capsule-tools "$task_producer/util/efi_capsule" \
 --compiler /usr/bin/x86_64-linux-gnu-gcc-15 --expect-local-floor-refusal \
 --binding "$task_output/run-cold.sh" --binding "$task_output/source-head.txt" \
 --binding "$task_output/source-status.txt" \
 --binding "$task_warm/initial9/full.config" --binding "$task_warm/targetA/full.config" \
 --binding "$task_warm/run-native.sh" --binding "$task_warm/validate-fixtures.sh" \
 --binding /home/sean/normal-refusal-consumed-producer.VPNIdB/build-producer.sh \
 > "$task_output/native.log" 2>&1
