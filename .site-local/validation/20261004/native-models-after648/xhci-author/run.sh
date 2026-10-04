#!/bin/bash
set -u
root=/home/sean/Documents/.cdk2-worktrees/xhci-private-model-types
receipt=$root/.xhci-gate-recovery.4UFYH6
cd "$root"
snapshot() {
 git diff | sha256sum
 git ls-files include src tests util Makefile Kconfig defconfig | xargs sha256sum
 for tool in cc gcc ld objcopy readelf nm make kconfig-conf perl; do
  path=$(command -v "$tool"); printf '%s %s\n' "$tool" "$path"; test -z "$path" || sha256sum "$(readlink -f "$path")"
 done
 cc --version; make --version
}
snapshot > "$receipt/before.txt"
run() {
 label=$1; shift
 "$@" > "$receipt/$label.log" 2>&1
 result=$?
 printf '%s=%s\n' "$label" "$result" | tee -a "$receipt/status"
}
for mode in default o0 o2 strict; do
 build=$receipt/$mode
 flags='-std=c11 -O2 -Wall -Wextra -Werror -fshort-wchar'
 case $mode in
 o0) flags='-std=c11 -O0 -g -Wall -Wextra -Werror -fshort-wchar -fno-omit-frame-pointer';;
 o2) flags='-std=c11 -O2 -g -Wall -Wextra -Werror -fshort-wchar -fno-omit-frame-pointer';;
 strict) flags='-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -fshort-wchar';;
 esac
 run "$mode-defconfig" make CDK2_BUILD_DIR="$build" defconfig
 run "$mode-native-xhci" make CDK2_BUILD_DIR="$build" CDK2_NATIVE_HOST_CFLAGS="$flags" native-xhci-test
done
run default-diagnostic-parity make CDK2_BUILD_DIR="$receipt/default" native-xhci-diagnostic-parity
base=$receipt/base
run base-worktree git worktree add --detach "$base" HEAD
run base-defconfig make -C "$base" CDK2_BUILD_DIR="$receipt/base-build" defconfig
run base-strict-native-xhci make -C "$base" CDK2_BUILD_DIR="$receipt/base-strict" defconfig native-xhci-test 'CDK2_NATIVE_HOST_CFLAGS=-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion -fshort-wchar'
run same-config-header cmp "$receipt/default/include/cdk2/config.h" "$receipt/base-build/include/cdk2/config.h"
for file in model controller pci_adapter usb2_abi diagnostic entry; do
 for side in candidate base; do
  tree=$root; build=$receipt/default
  if test "$side" = base; then tree=$base; build=$receipt/base-build; fi
  run "$side-$file-object" make -C "$tree" CDK2_BUILD_DIR="$build" "$build/native/xhci-$file.o"
 done
 run "$file-object-cmp" cmp "$receipt/default/native/xhci-$file.o" "$receipt/base-build/native/xhci-$file.o"
done
git diff --name-only | while read -r file; do
 run "checkpatch-${file//\//_}" util/lint/cdk2-checkpatch --no-tree --file "$file"
done
snapshot > "$receipt/after.txt"
run source-tool-closure cmp "$receipt/before.txt" "$receipt/after.txt"
