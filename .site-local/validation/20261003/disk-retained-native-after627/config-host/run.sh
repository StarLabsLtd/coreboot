#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=/home/sean/Documents/.cdk2-worktrees/disk-retained-four-epoch-observer-after623
output=/home/sean/disk-retained-config-host.okEESf
cd "$root"
sha256sum tests/system_fmp_disk_retained_native_test.py tests/system_fmp_disk_retained_runner_test.py \
 tests/system_fmp_disk_retained_store.c tests/system_fmp_disk_retained_store_test.sh \
 tests/system_fmp_core_ram_native_test.py tests/system_fmp_core_ram_refusal.py \
 util/qemu/bin/qmp_cbmem_console.py "$(command -v python3)" "$output/run.sh" \
 > "$output/inputs-before.sha256"
git log -1 --format='%H %G? %P' > "$output/source.txt"
git diff -- tests/system_fmp_disk_retained_native_test.py tests/system_fmp_disk_retained_runner_test.py > "$output/config-correction.diff"
sh -n tests/system_fmp_disk_retained_store_test.sh
git diff --check
set +e
/usr/bin/time -o "$output/host.time" -f 'WALL=%e USER=%U SYS=%S EXIT=%x' \
 python3 -B tests/system_fmp_disk_retained_runner_test.py > "$output/host.log" 2>&1
status=$?
set -e
printf '%s\n' "$status" > "$output/host.status"
sha256sum -c "$output/inputs-before.sha256" > "$output/inputs-after-check.log"
exit "$status"
