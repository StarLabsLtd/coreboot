#!/bin/bash
set -euo pipefail
receipt=/home/sean/joined-runtime-capture-tpm-ready652.ASlZmW
source=/home/sean/Documents/.cdk2-worktrees/runtime-capture-tpm-tests-after651-ready
canonical=/home/sean/Documents/cdk2/util/qemu/fixtures
cd "$source"
trap 'printf "%s\n" "$?" > "$receipt/aggregate.status"' EXIT
git rev-parse HEAD > "$receipt/head-before"
test -z "$(git status --porcelain)"
for name in nvme-final-c4bec.raw backup-gpt-linux.raw fat16-fat32-label-linux.raw capsule-valid.raw capsule-bad-signature.raw; do
	target="$source/util/qemu/fixtures/$name"
	test ! -e "$target" && test ! -L "$target"
	cp -p "$canonical/$name" "$target"
	cmp "$canonical/$name" "$target"
done
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' | xargs -d '\n' sha256sum > "$receipt/source-before.sha256"
sha256sum "$0" /usr/bin/python3.13 /usr/bin/bash > "$receipt/tools-before.sha256"
for test in assert-tpm-absent-dma-safe-selftest run-linear-acceptance-selftest assert-tpm-event-log-selftest freeze-tpm-evidence-selftest; do
	python3 -B "util/qemu/bin/$test.py" > "$receipt/$test.log" 2>&1
	printf '%s=0\n' "$test" >> "$receipt/status"
done
python3 -B tests/setup_capture_readiness_test.py > "$receipt/capture.log" 2>&1
printf 'capture=0\n' >> "$receipt/status"
for test in fresh_normal_fwui_admission normal_fwui_admission default_zero_setup_controller linux_fwui_reset; do
	python3 -B -m unittest discover -s tests -p "${test}_test.py" > "$receipt/$test.log" 2>&1
	printf '%s=0\n' "$test" >> "$receipt/status"
done
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log"
sha256sum -c "$receipt/tools-before.sha256" > "$receipt/tools-after-check.log"
git rev-parse HEAD > "$receipt/head-after"
cmp "$receipt/head-before" "$receipt/head-after"
test -z "$(git status --porcelain)"
