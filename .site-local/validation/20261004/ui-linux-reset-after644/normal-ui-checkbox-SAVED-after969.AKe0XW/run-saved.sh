#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Qualified saved-only replay of genuine session8717; original aggregate1 stays.
# No QEMU/guest launch, firmware build, seed or mutation of original receipts.
set -euo pipefail
stage=/home/sean/normal-ui-checkbox-SAVED-after969.AKe0XW
source=/home/sean/Documents/.cdk2-worktrees/normal-ui-manifest-int-after969
original=/home/sean/normal-ui-checkbox-native-after969.eAhimP
build=/home/sean/native-default-zero-hotkey-after628.CfqEvm
export COREBOOT_TREE=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
export CDK2_AB_CBFSTOOL="$build/initial9/build/util/cbfstool/cbfstool"
export CDK2_NORMAL_FWUI_INPUT_RECEIPTS="$stage/codec-compile-inputs"
export TMPDIR=/home/sean
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
unset PYTHONOPTIMIZE
ulimit -c 0
cd "$source"
test "$(git rev-parse HEAD)" = 6e4405633cda8962f25fef84296b1f9251c55cd3
test -z "$(git status --porcelain)"
git verify-commit HEAD > "$stage/signature.log" 2>&1
git rev-parse HEAD > "$stage/head-before"
git status --porcelain > "$stage/status-before"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	xargs -d '\n' sha256sum > "$stage/source-before.sha256"
find "$original" -type f -print0 | sort -z | xargs -0 sha256sum > "$stage/original-before.sha256"
sha256sum "$stage/run-saved.sh" /usr/bin/python3.13 /usr/bin/bash /usr/bin/time \
	/usr/bin/git /usr/bin/awk /usr/bin/find /usr/bin/sort /usr/bin/xargs /usr/bin/sha256sum \
	/usr/bin/cmp /usr/bin/cp > "$stage/tools-recipe-before.sha256"
finish()
{
	status=$?
	trap - EXIT
	set +e
	for scope in source original tools-recipe; do
		sha256sum -c "$stage/$scope-before.sha256" > "$stage/$scope-after-check.log" 2>&1 || status=1
	done
	git rev-parse HEAD > "$stage/head-after"
	git status --porcelain > "$stage/status-after"
	cmp "$stage/head-before" "$stage/head-after" || status=1
	cmp "$stage/status-before" "$stage/status-after" || status=1
	printf '%s\n' "$status" > "$stage/aggregate.status"
	exit "$status"
}
trap finish EXIT
test ! -e "$stage/view"
cp -a "$original/lifecycle/ui-cancel" "$stage/view"
/usr/bin/time -p -o "$stage/admission.time" python3 -B - "$original" "$stage" <<'PY' > "$stage/admission.log" 2>&1
import importlib.util, json, sys
from pathlib import Path
source = Path.cwd()
sys.path.insert(0, str(source / 'util/qemu/bin'))
spec = importlib.util.spec_from_file_location('ui_saved_runner', source / 'util/qemu/bin/run-normal-boot-to-fw-ui.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
original, stage = map(Path, sys.argv[1:])
inputs = {}
for directory in (original / 'lifecycle', original / 'lifecycle/ui-cancel'):
    before = json.loads((directory / 'inputs-before.json').read_bytes())
    assert isinstance(before, dict) and before and before == json.loads((directory / 'inputs-after.json').read_bytes())
    for path, digest in before.items():
        assert path not in inputs or inputs[path] == digest
        inputs[path] = digest
assert runner.snapshot(map(Path, inputs)) == inputs
aliases = json.loads((original / 'lifecycle/tool-aliases-before.json').read_bytes())
assert aliases == json.loads((original / 'lifecycle/tool-aliases-after.json').read_bytes())
assert runner.aliases(map(Path, inputs)) == aliases
view = stage / 'view'
manifest = json.loads((view / 'manifest.json').read_bytes())
runner.ui_cancel_admission(view, Path(manifest['rom']), Path(manifest['nvme_seed']),
    Path(manifest['payload_config_binding']['source']), Path('/home/sean/native-default-zero-hotkey-after628.CfqEvm/initial9/build/util/cbfstool/cbfstool'))
print('SAVED-only admission/current input maps passed; session8717 guest42.135943/180; original aggregate1 preserved')
PY
/usr/bin/time -p -o "$stage/ui-oracle.time" python3 -B util/qemu/bin/assert-default-zero-setup-run.py \
	"$stage/view" default-zero-control-cancel > "$stage/ui-oracle.log" 2>&1
/usr/bin/time -p -o "$stage/media.time" bash util/qemu/bin/assert-protected-setup-store.sh \
	"$build/initial9/build/coreboot.rom" "$stage/view/pflash.rom" "$stage/view" \
	--normal-system-fmp-ui-cancel > "$stage/media.log" 2>&1
