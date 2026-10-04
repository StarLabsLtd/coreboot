#!/bin/bash
set -u
root=/home/sean/Documents/.cdk2-worktrees/xhci-private-model-types
r=$root/.xhci-gate-recovery.4UFYH6
cd "$root"
run() { label=$1; shift; "$@" > "$r/$label.log" 2>&1; code=$?; printf '%s=%s\n' "$label" "$code" | tee -a "$r/status"; }
for file in model controller pci_adapter usb2_abi diagnostic entry; do
 run "base-$file-object-corrected" make -C "$r/base" CDK2_CONFIG_READY=1 CDK2_BUILD_DIR="$r/base-build" "$r/base-build/native/xhci-$file.o"
 run "$file-object-cmp-corrected" cmp "$r/default/native/xhci-$file.o" "$r/base-build/native/xhci-$file.o"
 for side in default base-build; do
  run "$side-$file-sections" readelf -SW "$r/$side/native/xhci-$file.o"
  run "$side-$file-symbols" nm -S "$r/$side/native/xhci-$file.o"
 done
done
run base-header-checkpatch "$r/base/util/lint/cdk2-checkpatch" --no-tree --file "$r/base/include/cdk2/xhci.h"
git diff > "$r/reviewed.patch"
run changed-lines-checkpatch util/lint/cdk2-checkpatch --no-tree --no-signoff "$r/reviewed.patch"
for side in candidate base; do
 tree=$root; build=$r/default
 if test "$side" = base; then tree=$r/base; build=$r/base-build; fi
 run "$side-layout-build" cc -std=c11 -Wall -Wextra -Werror -fshort-wchar -I"$tree/include" -I"$build/include" "$r/layout.c" -o "$r/$side-layout"
 run "$side-layout" "$r/$side-layout"
done
run layout-cmp cmp "$r/candidate-layout.log" "$r/base-layout.log"
git diff | sha256sum > "$r/final-diff.sha256"
