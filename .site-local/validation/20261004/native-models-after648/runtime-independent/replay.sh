#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/runtime-root-independent.6uKGVO
receipts=/home/sean/runtime2-author-gates-configfix.LZnLzl
for profile in default strict; do
  for opt in O0 O2; do
    for consumer in model entry; do
      executable="$receipts/$profile-$opt-$consumer"
      sha256sum "$executable" >> "$stage/inputs-before.sha256"
      ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$executable" > "$stage/$profile-$opt-$consumer.log" 2>&1
      printf '%s-%s-%s=0\n' "$profile" "$opt" "$consumer" >> "$stage/status"
    done
  done
  cmp "$receipts/$profile-baseline-entry.o" "$receipts/$profile-candidate-entry.o"
  printf '%s-object-cmp=0\n' "$profile" >> "$stage/status"
done
sha256sum -c "$stage/inputs-before.sha256" > "$stage/inputs-after-check.log"
printf '0\n' > "$stage/aggregate.status"
