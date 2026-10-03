#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
ulimit -c 0
proof_dir=/home/sean/serial-io-final-proof.ChzR2V/causes
source_dir=/home/sean/Documents/.cdk2-worktrees/serial-io-validated-tuples-after592
config_dir=/home/sean/serial-io-native-gate.wYokoR
cd "$source_dir"
sha256sum src/modules/serial_io/serial_io.c tests/serial_io_test.c include/cdk2/uart.h \
 "$config_dir/resolved.config" "$config_dir/include/cdk2/config.h" > "$proof_dir/input-before.sha256"
cmp src/modules/serial_io/serial_io.c "$proof_dir/baseline.c"
for mode in 0 2; do
 cc -std=c11 -Wall -Wextra -Werror -fshort-wchar -O"$mode" \
  -I"$config_dir/include" -Iinclude "$proof_dir/positive/tests/serial_io_test.c" \
  -o "$proof_dir/positive-o$mode"
 "$proof_dir/positive-o$mode"
 while IFS="$(printf '\t')" read -r name message; do
  cmp "$proof_dir/baseline.c" "$proof_dir/$name/inverse.c"
  difference_status=0
  diff -u --label baseline --label mutant "$proof_dir/baseline.c" \
   "$proof_dir/$name/src/modules/serial_io/serial_io.c" \
   > "$proof_dir/$name-o$mode.patch" || difference_status=$?
  test "$difference_status" = 1
  patch -R -s -o "$proof_dir/$name-o$mode-reconstructed.c" \
   "$proof_dir/$name/src/modules/serial_io/serial_io.c" \
   "$proof_dir/$name-o$mode.patch"
  cmp "$proof_dir/baseline.c" "$proof_dir/$name-o$mode-reconstructed.c"
  cc -std=c11 -Wall -Wextra -Werror -fshort-wchar -O"$mode" \
   -I"$config_dir/include" -Iinclude "$proof_dir/$name/tests/serial_io_test.c" \
   -o "$proof_dir/$name-o$mode"
  status=0
  "$proof_dir/$name-o$mode" > "$proof_dir/$name-o$mode.log" 2>&1 || status=$?
  test "$status" = 134
  rg -F "$message" "$proof_dir/$name-o$mode.log"
  rg -F 'Assertion `condition' "$proof_dir/$name-o$mode.log"
  if rg -i 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$proof_dir/$name-o$mode.log"; then
   exit 1
  fi
  printf 'PASS %s O%s targeted134 reverseTU noSAN\n' "$name" "$mode"
 done < "$proof_dir/mutation-map.txt"
done
sha256sum -c "$proof_dir/input-before.sha256"
