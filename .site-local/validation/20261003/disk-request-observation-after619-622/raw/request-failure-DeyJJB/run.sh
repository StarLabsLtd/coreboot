#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
cdk2_source=/home/sean/Documents/.cdk2-worktrees/disk-capsule-os-request-after616
proof=/home/sean/disk-capsule-request-retry.DeyJJB
header=/home/sean/pci-bus-mtrr-audit-after614.kFd3bH/build/include/cdk2/config.h
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -c 0
source_hashes()
{
	while IFS= read -r -d '' path; do
		if test -f "$cdk2_source/$path"; then
			sha256sum "$cdk2_source/$path"
		fi
	done < <(git -C "$cdk2_source" ls-files -z)
}
git -C "$cdk2_source" rev-parse HEAD > "$proof/source-base.txt"
git -C "$cdk2_source" diff --binary > "$proof/source-before.patch"
source_hashes > "$proof/source-before.sha256"
sha256sum "$header" "$proof/run.sh" /usr/bin/cc /usr/bin/sh /usr/bin/awk \
	> "$proof/inputs-before.sha256"
grep -qx '#define CONFIG_CDK2_LINEAR_BOOT 1' "$header"
grep -qx '#define CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME 1' "$header"
for optimization in 0 2; do
	status=0
	/usr/bin/time -o "$proof/o$optimization.time" \
		-f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		sh "$cdk2_source/tests/dxe_core_capsule_disk_handoff_test.sh" "$header" cc \
		"-std=c11 -O$optimization -Wall -Wextra -Werror -fshort-wchar" \
		> "$proof/o$optimization.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$proof/o$optimization.status"
	if test "$status" -ne 0; then
		break
	fi
done
source_hashes > "$proof/source-after.sha256"
cmp "$proof/source-before.sha256" "$proof/source-after.sha256"
git -C "$cdk2_source" diff --binary > "$proof/source-after.patch"
cmp "$proof/source-before.patch" "$proof/source-after.patch"
sha256sum -c "$proof/inputs-before.sha256" > "$proof/inputs-after-check.log"
printf 'ACTUAL_OUTER_STATUS=%s\n' "$status"
exit "$status"
