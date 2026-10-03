#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
source=/home/sean/Documents/.cdk2-worktrees/default-zero-native-ui-observer-after628
proof=/home/sean/native-default-zero-hotkey-after628.CfqEvm/running-qmp-host
mkdir "$proof"
export TMPDIR=/home/sean
cd "$source"
git rev-parse HEAD > "$proof/head-before.txt"
git status --porcelain > "$proof/status-before.txt"
sha256sum util/qemu/bin/qmp-setup-acceptance.py util/qemu/bin/setup_acceptance_contract.py \
 util/qemu/bin/assert-default-zero-setup-run.py tests/default_zero_setup_controller_test.py \
 util/qemu/bin/selftest.sh util/qemu/bin/qmp_cbmem_console.py \
 tests/qmp_cbmem_console_test.py /usr/bin/python3.13 /usr/bin/time \
 /home/sean/native-default-zero-hotkey-after628.CfqEvm/run-running-qmp-host.sh \
 > "$proof/inputs-before.sha256"
/usr/bin/time -o "$proof/new.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
 python3 tests/default_zero_setup_controller_test.py > "$proof/new.log" 2>&1
/usr/bin/time -o "$proof/console.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
 python3 tests/qmp_cbmem_console_test.py > "$proof/console.log" 2>&1
sha256sum -c "$proof/inputs-before.sha256" > "$proof/inputs-after-check.log"
git rev-parse HEAD > "$proof/head-after.txt"
git status --porcelain > "$proof/status-after.txt"
cmp "$proof/head-before.txt" "$proof/head-after.txt"
cmp "$proof/status-before.txt" "$proof/status-after.txt"
git diff --check
