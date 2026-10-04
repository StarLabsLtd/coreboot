#!/usr/bin/env bash
# Root-released existing O0/O2 HOST models only; no compiler/VM/media writes.
set -euo pipefail
review=/home/sean/fresh-normal-guest-independent-review654.o7hehD
guest=/home/sean/fresh-normal-joined-guest-after654.Vr3XYD/linux-reset
run=$guest/lifecycle/same-vm
codec=$guest/lifecycle/codec/linux-reset-store.ZQyk9P
firmware=/home/sean/Documents/cdk2
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
stage=/home/sean/fresh-normal-after654.nP6Vda/build
output=$review/linux-built-model-replay
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean LANG=C LC_ALL=C
unset PYTHONOPTIMIZE
ulimit -c 0
test "$(git -C "$firmware" rev-parse HEAD)" = 0bb127b503d8c5d973b07d608f564ff0ef2ef6bb
test -z "$(git -C "$firmware" status --porcelain)"
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
test "$(cat "$run/qemu.status")" = 0
test "$(cat "$run/observer.status")" = 0
test "$(cat "$run/outer.status")" = 0
mkdir "$output"
trap 'printf "%s\n" "$?" > "$output/aggregate.status"' EXIT
sha256sum "$0" "$stage/initial9/build/coreboot.rom" "$run/pflash-before.rom" \
  "$run/pflash.rom" "$codec/initial.bin" "$codec/final.bin" \
  "$codec/initial.config" "$codec/final.config" "$run/coreboot-config.txt" \
  "$codec/check-o0" "$codec/check-o2" "$codec/source-before.sha256" \
  "$codec/executables-before.sha256" "$codec/mutant.log" \
  "$codec/comparison-inverse.c" > "$output/inputs-before.sha256"
for tool in env bash git sha256sum cmp cut time realpath; do
  sha256sum "$(realpath -e "$(type -P "$tool")")"
done > "$output/tools-before.sha256"
cmp "$firmware/tests/linux_fwui_reset_media_check.c" "$codec/comparison-inverse.c"
cmp "$codec/initial.config" "$codec/final.config"
cmp "$codec/initial.config" "$run/coreboot-config.txt"
sha256sum -c "$codec/source-before.sha256" > "$output/source-before-check.log"
sha256sum -c "$codec/executables-before.sha256" > "$output/executables-before-check.log"
for optimization in 0 2; do
  sha256sum -c "$codec/check-o$optimization-inputs/inputs-before.sha256" \
    > "$output/o$optimization-inputs-before-check.log"
  sha256sum -c "$codec/check-o$optimization-inputs/tools-before.sha256" \
    > "$output/o$optimization-tools-before-check.log"
done
aggregate=0
for optimization in 0 2; do
  command=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C
    ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1
    "$codec/check-o$optimization" "$codec/initial.bin" "$codec/final.bin"
    00112233-4455-6677-8899-aabbccddeeff --self-test)
  printf '%q ' "${command[@]}" > "$output/o$optimization.command.txt"
  printf '\n' >> "$output/o$optimization.command.txt"
  status=0
  /usr/bin/time -o "$output/o$optimization.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
    "${command[@]}" > "$output/o$optimization.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$output/o$optimization.status"
  if ((status != 0)); then aggregate=1; fi
done
sha256sum -c "$output/inputs-before.sha256" > "$output/inputs-after-check.log"
sha256sum -c "$output/tools-before.sha256" > "$output/tools-after-check.log"
sha256sum -c "$codec/source-before.sha256" > "$output/source-after-check.log"
sha256sum -c "$codec/executables-before.sha256" > "$output/executables-after-check.log"
for optimization in 0 2; do
  sha256sum -c "$codec/check-o$optimization-inputs/inputs-before.sha256" \
    > "$output/o$optimization-inputs-after-check.log"
  sha256sum -c "$codec/check-o$optimization-inputs/tools-before.sha256" \
    > "$output/o$optimization-tools-after-check.log"
done
test "$(git -C "$firmware" rev-parse HEAD)" = 0bb127b503d8c5d973b07d608f564ff0ef2ef6bb
test -z "$(git -C "$firmware" status --porcelain)"
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
printf '0\n' > "$output/closure.status"
test "$aggregate" = 0
