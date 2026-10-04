#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/checkpatch-byte-types-gates.QKhihW
cd /home/sean/Documents/.cdk2-worktrees/checkpatch-efi-byte-types-after645
sha256sum util/lint/cdk2-typedefs.checkpatch tests/checkpatch_filter_test.sh > "$stage/source-before.sha256"
/usr/bin/time -p -o "$stage/filter.time" sh tests/checkpatch_filter_test.sh > "$stage/filter.log" 2>&1
printf 'filter=0\n' > "$stage/status"
/usr/bin/time -p -o "$stage/xhci-header.time" util/lint/cdk2-checkpatch --no-tree --file /home/sean/Documents/.cdk2-worktrees/xhci-private-model-types/include/cdk2/xhci.h > "$stage/xhci-header.log" 2>&1
printf 'xhci-header=0\n' >> "$stage/status"
sha256sum -c "$stage/source-before.sha256" > "$stage/source-after-check.log"
printf '0\n' > "$stage/aggregate.status"
