#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Independent HOST replay only; no guest launch or firmware writes.
set -eu
task_dir=/home/sean/default-zero-motion-independent.VAy5gW
source_dir=/home/sean/Documents/.cdk2-worktrees/default-zero-native-ui-observer-after628
cd "$source_dir"
export PYTHONDONTWRITEBYTECODE=1
python_path=$(readlink -f "$(command -v python3)")
sha256sum \
 util/qemu/bin/qmp-setup-acceptance.py \
 tests/default_zero_setup_controller_test.py \
 util/qemu/bin/setup_acceptance_contract.py \
 util/qemu/bin/assert-default-zero-setup-run.py \
 util/qemu/bin/qmp_cbmem_console.py \
 tests/qmp_cbmem_console_test.py \
 util/qemu/bin/selftest.sh \
 /home/sean/default-zero-controller-source-peer.LfZshb/old-mode-parity.py \
 "$python_path" "$task_dir/run.sh" > "$task_dir/source-before.sha256"
/usr/bin/time -o "$task_dir/host.time" python3 tests/default_zero_setup_controller_test.py -v > "$task_dir/host.log" 2>&1
/usr/bin/time -o "$task_dir/reader.time" python3 tests/qmp_cbmem_console_test.py -v > "$task_dir/reader.log" 2>&1
/usr/bin/time -o "$task_dir/old-mode.time" python3 /home/sean/default-zero-controller-source-peer.LfZshb/old-mode-parity.py > "$task_dir/old-mode.log" 2>&1
git diff --check
bash -n util/qemu/bin/selftest.sh
sha256sum -c "$task_dir/source-before.sha256"
