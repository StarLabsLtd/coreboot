#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
test "$#" = 3
initial=$1
final=$2
cbfstool=$3
proof=/home/sean/os-indications-absence.jyU2Jp
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
test -f "$initial"
test -f "$final"
test -x "$cbfstool"
test ! -e "$proof/source-inputs-before.sha256"
compiler=$(command -v cc)
flags="-std=gnu11 -Wall -Wextra -Werror -g -fno-builtin -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DENV_TEST=1 -Du8=uint8_t -Du16=uint16_t -Du32=uint32_t -Du64=uint64_t -Ds32=int32_t -Ds64=int64_t"
includes="-include $producer/src/commonlib/bsd/include/commonlib/bsd/compiler.h -I$producer/src/commonlib/include -I$producer/src/commonlib/bsd/include -idirafter $producer/src/include"
sources="$proof/absence.c $producer/src/lib/payload_mm_authvar_fv.c $producer/src/lib/payload_mm_authvar_ftw.c $producer/src/lib/payload_mm_authvar_store.c"
# Compiler-reported actual transitive header/TU closure, not guessed headers.
cc $flags $includes -M $sources > "$proof/dependencies.raw"
tr '\\\n' '  ' < "$proof/dependencies.raw" | tr ' ' '\n' | sed '/^$/d; /:$/d' | sort -u > "$proof/dependencies.paths"
sha256sum "$proof/absence.c" "$proof/run.sh" "$compiler" "$initial" "$final" "$cbfstool" > "$proof/source-inputs-before.sha256"
while IFS= read -r dependency; do
	sha256sum "$dependency"
done < "$proof/dependencies.paths" >> "$proof/source-inputs-before.sha256"
"$cbfstool" "$initial" read -r SMMSTORE -f "$proof/initial-store.bin" > "$proof/extract-initial.log" 2>&1
"$cbfstool" "$final" read -r SMMSTORE -f "$proof/final-store.bin" > "$proof/extract-final.log" 2>&1
sha256sum "$proof/initial-store.bin" "$proof/final-store.bin" > "$proof/extracted.sha256"
for optimization in 0 2; do
	cc $flags $includes -O"$optimization" $sources -o "$proof/absence-o$optimization" > "$proof/compile-o$optimization.log" 2>&1
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$proof/absence-o$optimization" initial "$proof/initial-store.bin" > "$proof/initial-o$optimization.log" 2>&1
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$proof/absence-o$optimization" final "$proof/final-store.bin" > "$proof/final-o$optimization.log" 2>&1
done
sha256sum -c "$proof/source-inputs-before.sha256" > "$proof/source-inputs-after.log"
sha256sum -c "$proof/extracted.sha256" > "$proof/extracted-after.log"
