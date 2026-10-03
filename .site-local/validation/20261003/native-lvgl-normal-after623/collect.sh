#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
packet=/home/sean/native-lvgl623-public.qKgLmg/native-lvgl-normal-after623
source=/home/sean/Documents/.cdk2-worktrees/phase-owned-lvgl-status-after614
mkdir -p "$packet/sources"
cp -p /home/sean/native-lvgl623-public.qKgLmg/collect.sh "$packet/collect.sh"
: > "$packet/original-copies.tsv"
for profile in default-zero interactive-two; do
  case "$profile" in
    default-zero) original=/home/sean/native-lvgl-normal-after623.zLwvcS ;;
    interactive-two) original=/home/sean/native-lvgl-interactive-after623.fXB54P ;;
  esac
  files=(build-core.sh build-producer.sh prepare-reference.py run-ui.py check-ui.py
    build.log build.time outer.status source-head.txt source-before.sha256
    source-after-check.log source-after.status vendor-heads.txt inputs-before.sha256
    inputs-after-check.log outputs.sha256 resolved.config include/cdk2/config.h
    reference.log reference.time reference-build.log reference-source-identities.json
    reference-config-before-after.json native-ui.log native-ui.time
    check-lossless-screens.py lossless-screens.json
    initial9/full.config initial9/input.config initial9/configure.log initial9/build.log
    initial9/build.time initial9/source-head.txt initial9/source-before.sha256
    initial9/source-after-check.log initial9/inputs-before.sha256
    initial9/inputs-after-check.log initial9/outputs.sha256
    kconfig/Kconfig kconfig/defconfig kconfig/coreboot-input.identity kconfig/coreboot-source.tmp
    native/cdk2-coreboot-image.map native/native-direct-image-inventory.tsv
    native/native-direct-composition-inventory.tsv native/native-direct-images.rsp
    ui-run/result.json ui-run/command.json ui-run/observer.log ui-run/qemu.log
    ui-run/serial.log ui-run/inputs-before.json ui-run/inputs-after.json
    ui-run/coreboot-config.txt ui-run/cbmem-console.bin ui-run/cbmem-live.log
    ui-run/cbmem-observer.json ui-run/cbmem-table-0.bin ui-run/cbmem-table-1.bin
    ui-run/selecting-reference.ppm ui-run/selecting-reference-manifest.json
    ui-run/reference-raster/qemu-profile-geometry.json
    ui-run/reference-raster/qemu-profile-status-1.ppm)
  if test "$profile" = default-zero; then
    files+=(prepare-reference-uninitialized-wt.py reference-uninitialized-wt.log
      reference-uninitialized-wt.time reference-uninitialized-wt-build.log)
  else
    files+=(interactive-input.config check-profile.py profile-check.json
      profile-before-vm.json config-delta.diff check-ui.log pixel-oracle-source-binding.json
      ui-run/qmp-setup-evidence.json)
  fi
  for capture in "$original"/ui-run/setup-*.ppm "$original"/ui-run/setup-*.png \
    "$original"/ui-run/cbmem-scratch/*; do
    files+=("${capture#"$original"/}")
  done
  for name in "${files[@]}"; do
    test -f "$original/$name" && test ! -L "$original/$name"
    target="$profile/$name"
    mkdir -p "$packet/$(dirname "$target")"
    cp -p "$original/$name" "$packet/$target"
    digest=$(sha256sum "$original/$name"); digest=${digest%% *}
    printf '%s\t%s\t%s\n' "$target" "$original/$name" "$digest" >> "$packet/original-copies.tsv"
  done
done
head=cb71f948001ddfcc2b366d645b12a61e08d3ff1a
test "$(git -C "$source" rev-parse HEAD)" = "$head"
test -z "$(git -C "$source" status --porcelain)"
paths=(Makefile src/boot/Makefile src/modules/dxe_core/entry.c
  src/modules/dxe_core/image.c include/cdk2/lvgl_ui.h
  src/modules/dxe_core/private_control.h tests/splash_status_report_test.c
  tests/splash_status_report_test.sh tests/lvgl_ui_driver_test.c
  src/modules/lvgl_renderer/driver.c src/modules/lvgl_setup/settings.c
  src/lib/boot_logo.c configs/lvgl/lv_conf.h tests/lvgl_dependency_provenance_test.sh
  util/qemu/bin/assert-setup-run.py util/qemu/bin/qmp-setup-acceptance.py
  util/qemu/bin/qmp_cbmem_console.py util/qemu/bin/setup_acceptance_contract.py)
git -C "$source" archive "$head" "${paths[@]}" | zstd -q -f -o "$packet/sources/consumer623.tar.zst"
git -C "$source" log -1 --format='%H%n%G?%n%s' > "$packet/sources/consumer623-commit.txt"
: > "$packet/sources/identities.tsv"
for name in "${paths[@]}"; do
  digest=$(git -C "$source" show "$head:$name" | sha256sum); digest=${digest%% *}
  printf '%s\t%s\t%s\n' "$head" "$name" "$digest" >> "$packet/sources/identities.tsv"
done
