#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=/home/sean/Documents/.cdk2-worktrees/disk-retained-four-epoch-observer-after623
output=/home/sean/normal-disk-retained-native.dSunYE
producer=/home/sean/normal-efi-disk-producers-after625.ZBl2DT
core=/home/sean/normal-efi-disk-core-final.TJMaS8
app=/home/sean/normal-efi-disk-request-after624-build.E29WEn
tools=/home/sean/native-lvgl-interactive-after623.fXB54P/native
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
cd "$root"
test "$(git rev-parse HEAD)" = 0647ce39aa9a4b95fbceeecee59d6cfd16199064
test -z "$(git status --porcelain)"
test ! -e "$output/run-1"
sha256sum -c "$core/outputs.sha256" > "$output/core-input-check.log"
sha256sum -c "$app/inputs-before.sha256" > "$output/app-input-check.log"
sha256sum -c "$app/tools-before.sha256" > "$output/app-tools-check.log"
sha256sum -c "$producer/initial9/outputs.sha256" > "$output/initial-input-check.log"
sha256sum -c "$producer/targetA/outputs.sha256" > "$output/target-input-check.log"
sha256sum -c "$producer/capsule.sha256" > "$output/capsule-input-check.log"
/usr/bin/time -o "$output/native.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
 python3 -B tests/system_fmp_disk_retained_native_test.py \
 --initial "$producer/initial9/build/coreboot.rom" \
 --target "$producer/targetA/build/coreboot.rom" \
 --initial-config "$producer/initial9/full.config" --target-config "$producer/targetA/full.config" \
 --capsule "$producer/disk-a.cap" \
 --trust /home/sean/q35-capsule-native-signing.PuAsza/trust.pem \
 --capsule-tools /home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388/util/efi_capsule \
 --cbfstool "$producer/initial9/build/util/cbfstool/cbfstool" \
 --core "$core/native/cdk2-coreboot-image.elf" --config "$core/resolved.config" \
 --header "$core/include/cdk2/config.h" \
 --inventory "$core/native/native-direct-image-inventory.tsv" \
 --disk "$output/baseline-nvme.raw" --app "$app/NormalEfiDiskRequest.efi" \
 --compiler /usr/bin/cc --pe-audit "$tools/cdk2-pe-exec-sections" \
 --binding "$output/run-native.sh" --binding "$producer/build-producers.sh" \
 --binding "$producer/generate.sh" --binding "$producer/generation-before.sha256" \
 --binding "$producer/initial9/source-before.sha256" --binding "$producer/targetA/source-before.sha256" \
 --binding "$producer/initial9/inputs-before.sha256" --binding "$producer/targetA/inputs-before.sha256" \
 --binding "$core/source-before.sha256" --binding "$core/inputs-before.sha256" \
 --binding "$core/outputs.sha256" --binding "$core/native/native-direct-composition-inventory.tsv" \
 --binding "$app/inputs-before.sha256" --binding "$app/tools-before.sha256" \
 --binding /usr/bin/ld --binding /usr/bin/readelf \
 --binding "$tools/cdk2-native-pe-link" --binding "$tools/pe-relocation-marker.o" \
 --output "$output/run-1" > "$output/native.log" 2>&1
