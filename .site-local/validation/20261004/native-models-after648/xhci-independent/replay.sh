#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
source=/home/sean/Documents/.cdk2-worktrees/xhci-private-model-types
receipts="$source/.xhci-gate-recovery.4UFYH6"
stage=/home/sean/xhci-root-independent.Yh4rSb
test "$(git -C "$source" rev-parse HEAD)" = 9033db5d2ca36823c0d8834bf686778b19721060
git -C "$source" verify-commit HEAD > "$stage/signature.log" 2>&1
for mode in default o0 o2; do
  for name in model controller pci entry; do
    executable="$receipts/$mode/native/xhci-$name-test"
    sha256sum "$executable" >> "$stage/executables-before.sha256"
    ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$executable" > "$stage/$mode-$name.log" 2>&1
    printf '%s-%s=0\n' "$mode" "$name" >> "$stage/status"
  done
done
for name in model controller diagnostic; do
  cmp "$receipts/default/native/xhci-$name.o" "$receipts/base-build/native/xhci-$name.o"
  printf '%s-object-cmp=0\n' "$name" >> "$stage/status"
done
sha256sum -c "$stage/executables-before.sha256" > "$stage/executables-after-check.log"
printf '0\n' > "$stage/aggregate.status"
