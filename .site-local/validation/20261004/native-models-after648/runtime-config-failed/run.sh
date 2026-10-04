#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/runtime2-author-gates-retry.mIsvTK
source=/home/sean/Documents/.cdk2-worktrees/runtime-arch-entry-native-after645
baseline=/home/sean/Documents/cdk2
export PATH=/usr/local/bin:/usr/bin:/bin
unset MAKEFLAGS MFLAGS MAKEOVERRIDES CDK2_CONFIG CDK2_CONFIG_HEADER CDK2_CONFIG_READY CDK2_CONFIG_LOCK_FD
ulimit -c 0
cd "$source"
test "$(git rev-parse HEAD)" = ef4819bcba4a407922df89a30f2a9b99692820bc
test "$(git -C "$baseline" rev-parse HEAD)" = ef4819bcba4a407922df89a30f2a9b99692820bc
test -z "$(git -C "$baseline" status --porcelain)"
git rev-parse HEAD > "$stage/head-before"
git status --porcelain > "$stage/status-before"
git diff --binary > "$stage/frozen-before.patch"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' | xargs -d '\n' sha256sum > "$stage/source-before.sha256"
sha256sum "$baseline/src/modules/runtime_arch/entry.c" "$stage/run.sh" > "$stage/recipe-baseline-before.sha256"
tools=(/usr/bin/gcc /usr/bin/cc /usr/bin/as /usr/bin/ld /usr/bin/make /usr/bin/bash /usr/bin/sha256sum /usr/bin/cmp /usr/bin/git /usr/bin/sed /usr/bin/awk /usr/bin/xargs /usr/bin/realpath)
for tool in cc1 collect2 libasan.so libubsan.so libgcc_s.so; do
  if [[ $tool == cc1 || $tool == collect2 ]]; then file=$(gcc -print-prog-name="$tool"); else file=$(gcc -print-file-name="$tool"); fi
  test -f "$file"
  tools+=("$(realpath "$file")")
done
sha256sum "${tools[@]}" > "$stage/tools-before.sha256"
finish()
{
  result=$?
  trap - EXIT
  set +e
  for name in source recipe-baseline tools; do
    sha256sum -c "$stage/$name-before.sha256" > "$stage/$name-after-check.log" 2>&1 || result=1
  done
  git rev-parse HEAD > "$stage/head-after"
  git status --porcelain > "$stage/status-after"
  git diff --binary > "$stage/frozen-after.patch"
  cmp "$stage/head-before" "$stage/head-after" || result=1
  cmp "$stage/status-before" "$stage/status-after" || result=1
  cmp "$stage/frozen-before.patch" "$stage/frozen-after.patch" || result=1
  test "$(git -C "$baseline" rev-parse HEAD)" = ef4819bcba4a407922df89a30f2a9b99692820bc || result=1
  printf '%s\n' "$result" > "$stage/aggregate.status"
  exit "$result"
}
trap finish EXIT
compile()
{
  local receipt=$1
  shift
  mkdir "$receipt"
  printf '%s\0' gcc "$@" > "$receipt/argv.nul"
  # GCC's full -M closure includes system headers, not only repo-local -MMD.
  gcc "$@" -M -MF "$receipt/closure.mk" -MT gate > "$receipt/dependencies.log" 2>&1
  sed ':a;N;$!ba;s/\\\n/ /g' "$receipt/closure.mk" | cut -d: -f2- | tr ' ' '\n' | sed '/^$/d' | sort -u > "$receipt/dependencies.list"
  xargs -d '\n' sha256sum < "$receipt/dependencies.list" > "$receipt/inputs-before.sha256"
  gcc "$@" > "$receipt/compile.log" 2>&1
  sha256sum -c "$receipt/inputs-before.sha256" > "$receipt/inputs-after-check.log"
  printf '0\n' > "$receipt/compile.status"
}
for profile in default strict; do
  if [[ $profile == default ]]; then seed="$source/defconfig"; else seed="$source/util/qemu/config/cdk2-q35-acceptance.defconfig"; fi
  make --no-print-directory config CDK2_DEFCONFIG="$seed" CDK2_BUILD_DIR="$stage/$profile" > "$stage/$profile-config.log" 2>&1
  header="$stage/$profile/include/cdk2/config.h"
  sha256sum "$stage/$profile/.config" "$header" >> "$stage/config-before.sha256"
  for optimization in 0 2; do
    for consumer in model entry; do
      name="$profile-O$optimization-$consumer"
      output="$stage/$name"
      args=(-std=c11 -O"$optimization" -Wall -Wextra -Werror -fshort-wchar -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie -include "$header" -I"$source/include")
      if [[ $consumer == entry ]]; then
        args+=(-DCDK2_RUNTIME_ARCH_ENTRY_TEST "$source/tests/runtime_arch_entry_test.c" "$source/src/modules/runtime_arch/entry.c" "$source/src/lib/pe_image_view.c")
      else args+=("$source/tests/runtime_arch_test.c"); fi
      args+=("$source/src/modules/runtime_arch/runtime_arch.c" -o "$output")
      compile "$stage/$name-inputs" "${args[@]}"
      sha256sum "$output" >> "$stage/executables-before.sha256"
      ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$output" > "$stage/$name.log" 2>&1
      printf '%s=0\n' "$name" >> "$stage/status"
    done
  done
  for version in baseline candidate; do
    if [[ $version == baseline ]]; then tree="$baseline"; else tree="$source"; fi
    output="$stage/$profile-$version-entry.o"
    compile "$stage/$profile-$version-native-inputs" -ffreestanding -fno-builtin -fno-stack-protector -fpie -mcmodel=small -fvisibility=hidden -include "$tree/include/cdk2/native_visibility.h" -fno-asynchronous-unwind-tables -fno-unwind-tables -fshort-wchar -m64 -mno-red-zone -mno-sse -mno-mmx -Os -Wall -Werror -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-ident -fcf-protection=none -maccumulate-outgoing-args -I"$stage/$profile/include" -I"$tree/include" -c "$tree/src/modules/runtime_arch/entry.c" -o "$output"
  done
  cmp "$stage/$profile-baseline-entry.o" "$stage/$profile-candidate-entry.o"
  printf '%s-whole-native-object-cmp=0\n' "$profile" >> "$stage/status"
done
sha256sum -c "$stage/config-before.sha256" > "$stage/config-after-check.log"
sha256sum -c "$stage/executables-before.sha256" > "$stage/executables-after-check.log"
printf 'Runtime2 existing model/entry ASAN+UBSAN O0/O2 default+strict and whole native object identity PASS\n'

