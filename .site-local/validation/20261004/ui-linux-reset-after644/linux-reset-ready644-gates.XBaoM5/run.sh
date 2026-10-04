#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -uo pipefail
stage=/home/sean/linux-reset-ready644-gates.XBaoM5
cd /home/sean/Documents/.cdk2-worktrees/linux-fwui-reset-after643-ready || exit 1
git rev-parse HEAD > "$stage/head-before"
git status --porcelain > "$stage/status-before"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' | xargs -d '\n' sha256sum > "$stage/source-before.sha256"
status=0
for name in linux_fwui_reset default_zero_setup_controller qmp_cbmem_console normal_fwui_inputs normal_fwui_execution normal_fwui_admission linux_fwui_request; do
  /usr/bin/time -p -o "$stage/$name.time" python3 -B -m unittest discover -s tests -p "${name}_test.py" > "$stage/$name.log" 2>&1
  result=$?
  printf '%s=%s\n' "$name" "$result" >> "$stage/status"
  test "$result" = 0 || status=1
done
for mode in linux ui; do
  if test "$mode" = linux; then
    view=/home/sean/linux-fwui-epoch-native-after4bb.j6OTbN/lifecycle/same-vm
    profile=default-zero-linux-fw-ui-reset
  else
    view=/home/sean/normal-ui-checkbox-native-after969.eAhimP/lifecycle/ui-cancel
    profile=default-zero-control-cancel
  fi
  /usr/bin/time -p -o "$stage/$mode-saved.time" python3 -B util/qemu/bin/assert-default-zero-setup-run.py "$view" "$profile" > "$stage/$mode-saved.log" 2>&1
  result=$?
  printf '%s-saved=%s\n' "$mode" "$result" >> "$stage/status"
  test "$result" = 0 || status=1
done
export COREBOOT_TREE=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
export CDK2_AB_CBFSTOOL=/home/sean/native-default-zero-hotkey-after628.CfqEvm/initial9/build/util/cbfstool/cbfstool
export CDK2_NORMAL_FWUI_INPUT_RECEIPTS="$stage/codec-inputs"
view=/home/sean/linux-fwui-epoch-native-after4bb.j6OTbN/lifecycle/same-vm
/usr/bin/time -p -o "$stage/linux-media.time" bash util/qemu/bin/assert-linux-fw-ui-reset-store.sh "$view/pflash-before.rom" "$view/pflash.rom" "$stage/codec" > "$stage/linux-media.log" 2>&1
result=$?
printf 'linux-media=%s\n' "$result" >> "$stage/status"
test "$result" = 0 || status=1
sha256sum -c "$stage/source-before.sha256" > "$stage/source-after-check.log" 2>&1 || status=1
git rev-parse HEAD > "$stage/head-after"
git status --porcelain > "$stage/status-after"
cmp "$stage/head-before" "$stage/head-after" || status=1
cmp "$stage/status-before" "$stage/status-after" || status=1
printf '%s\n' "$status" > "$stage/aggregate.status"
exit "$status"
