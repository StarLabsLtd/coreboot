#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# The prior moving-HEAD parity failure is preserved in old-mode.log/time.
set -eu
task_dir=/home/sean/default-zero-motion-independent.VAy5gW
cd /home/sean/Documents/.cdk2-worktrees/default-zero-native-ui-observer-after628
export PYTHONDONTWRITEBYTECODE=1
sha256sum "$task_dir/signed956-parity.py" "$task_dir/finish.sh" > "$task_dir/finish-before.sha256"
/usr/bin/time -o "$task_dir/signed956-parity.time" python3 "$task_dir/signed956-parity.py" > "$task_dir/signed956-parity.log" 2>&1
git diff --check
bash -n util/qemu/bin/selftest.sh
sha256sum -c "$task_dir/source-before.sha256"
sha256sum -c "$task_dir/finish-before.sha256"
