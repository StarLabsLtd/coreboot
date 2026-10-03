#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail
source_tree=/home/sean/Documents/.cdk2-worktrees/default-zero-native-ui-observer-after628
receipt=/home/sean/default-zero-queued-independent.00xTk5
export PYTHONDONTWRITEBYTECODE=1
export TMPDIR=/home/sean
cd "$source_tree"
git rev-parse HEAD > "$receipt/head-before.txt"
git status --porcelain > "$receipt/status-before.txt"
test "$(cat "$receipt/head-before.txt")" = f4d82376db41b79aedf1c81026f3db0ec601e41a
sha256sum util/qemu/bin/qmp-setup-acceptance.py \
    util/qemu/bin/setup_acceptance_contract.py \
    util/qemu/bin/assert-default-zero-setup-run.py \
    tests/default_zero_setup_controller_test.py \
    util/qemu/bin/selftest.sh util/qemu/bin/qmp_cbmem_console.py \
    tests/qmp_cbmem_console_test.py /usr/bin/python3.13 /usr/bin/time \
    /home/sean/native-default-zero-hotkey-after628.CfqEvm/run-queued-host.sh \
    "$receipt/run.sh" > "$receipt/inputs-before.sha256"
/usr/bin/time -o "$receipt/controller.time" /usr/bin/python3.13 \
    tests/default_zero_setup_controller_test.py > "$receipt/controller.log" 2>&1
/usr/bin/time -o "$receipt/reader.time" /usr/bin/python3.13 \
    tests/qmp_cbmem_console_test.py > "$receipt/reader.log" 2>&1
sha256sum -c "$receipt/inputs-before.sha256" > "$receipt/inputs-after-check.log"
git rev-parse HEAD > "$receipt/head-after.txt"
git status --porcelain > "$receipt/status-after.txt"
cmp "$receipt/head-before.txt" "$receipt/head-after.txt"
cmp "$receipt/status-before.txt" "$receipt/status-after.txt"
git diff --check
