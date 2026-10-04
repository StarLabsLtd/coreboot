#!/usr/bin/env bash
# Released HOST models and ONE source-owned xcompile-only Make probe.
# Never invokes producer all, Core, kernel or VM targets.
set -uo pipefail
receipt=/home/sean/fresh-producer-xcompile-final.UGloK0
author=/home/sean/Documents/.cdk2-worktrees/fresh-producer-xcompile-after652
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
original=/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config
vboot=/home/sean/Documents/coreboot/3rdparty/vboot
stage=$receipt/stage
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean LANG=C LC_ALL=C
ulimit -c 0
cd "$author" || exit 1
snapshot_source()
{
	for source in "$author" "$producer"; do
		git -C "$source" ls-files --stage |
			awk '$1 == "100644" || $1 == "100755" {sub(/^[^\t]*\t/, ""); print}' |
			while IFS= read -r path; do sha256sum "$source/$path"; done
	done
}
snapshot_tools()
{
	for tool in python3 bash sh git sha256sum awk xargs cmp realpath tee time make gcc g++ \
		cc x86_64-linux-gnu-gcc x86_64-linux-gnu-cpp x86_64-linux-gnu-as x86_64-linux-gnu-ld.bfd \
		x86_64-linux-gnu-gcc-nm x86_64-linux-gnu-gcc-ar objcopy objdump readelf strip nm ar \
		head tail sed grep cut tr date uname mkdir rm mv cp mktemp cat sort; do
		alias=$(type -P "$tool") || return
		actual=$(realpath -e "$alias") || return
		printf 'TOOL\t%s\t%s\t%s\n' "$tool" "$alias" "$actual"
		sha256sum "$actual" || return
	done
}
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c || exit 1
test -z "$(git -C "$producer" status --porcelain)" || exit 1
git -C "$producer" verify-commit HEAD > "$receipt/producer-signature.log" 2>&1 || exit 1
test "$(sha256sum "$original" | cut -d ' ' -f1)" = d2f5db5a8d33dab72cca9587ca958e67c657b1d2ea0bec18ce69bff775bd1492 || exit 1
mkdir -p "$stage/initial9/build" || exit 1
cp -p "$original" "$stage/initial9/full.config" || exit 1
snapshot_source > "$receipt/source-before.sha256" || exit 1
snapshot_tools > "$receipt/tools-before.tsv" || exit 1
sha256sum "$receipt/run.sh" "$original" "$stage/initial9/full.config" > "$receipt/inputs-before.sha256"
git diff --binary | sha256sum > "$receipt/freeze-before.sha256"
git status --porcelain > "$receipt/status-before.txt"
status=0
for name in fresh_normal_fwui_admission_test normal_fwui_admission_test; do
	/usr/bin/time -p -o "$receipt/$name.time" python3 -B "tests/$name.py" > "$receipt/$name.log" 2>&1
	result=$?
	printf '%s=%s\n' "$name" "$result" | tee -a "$receipt/status"
	test "$result" = 0 || status=1
done
command=(env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C make -C "$producer" -j2
	UPDATED_SUBMODULES=1 obj="$stage/initial9/build" DOTCONFIG="$stage/initial9/full.config"
	VBOOT_SOURCE="$vboot" HOSTCC=gcc HOSTCXX=g++ "$stage/initial9/build/xcompile")
python3 -B "$author/util/qemu/bin/fresh_normal_fwui.py" record-command "$stage" initial9/toolchain.argv.json "${command[@]}" || status=1
/usr/bin/time -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' -o "$receipt/xcompile.time" \
	"${command[@]}" > "$receipt/xcompile.log" 2>&1
result=$?
printf '%s\n' "$result" > "$receipt/xcompile.status"
printf 'actual-xcompile-only=%s\n' "$result" | tee -a "$receipt/status"
test "$result" = 0 || status=1
if test "$result" = 0; then
	python3 -B - "$author" "$stage" > "$receipt/selected-tools-before.json" 2> "$receipt/selected-tools.log" <<'PY'
import json, pathlib, sys
sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / 'util/qemu/bin'))
import fresh_normal_fwui as fresh
print(json.dumps(fresh.producer_toolchain(pathlib.Path(sys.argv[2]), fresh.helpers()), indent=2, sort_keys=True))
PY
	result=$?
	printf 'actual-selected-tools=%s\n' "$result" | tee -a "$receipt/status"
	test "$result" = 0 || status=1
	if test "$result" = 0; then
		python3 -B - "$author" "$stage" > "$receipt/selected-tools-after.json" <<'PY'
import json, pathlib, sys
sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / 'util/qemu/bin'))
import fresh_normal_fwui as fresh
print(json.dumps(fresh.producer_toolchain(pathlib.Path(sys.argv[2]), fresh.helpers()), indent=2, sort_keys=True))
PY
		cmp "$receipt/selected-tools-before.json" "$receipt/selected-tools-after.json" || status=1
	fi
fi
bash -n util/qemu/bin/build-normal-fwui-fresh.sh || status=1
python3 -B -c 'import ast,pathlib; [ast.parse(pathlib.Path(p).read_text()) for p in ("util/qemu/bin/fresh_normal_fwui.py","tests/fresh_normal_fwui_admission_test.py")]' || status=1
git diff --check || status=1
sha256sum -c "$receipt/source-before.sha256" > "$receipt/source-after-check.log" 2>&1 || status=1
sha256sum -c "$receipt/inputs-before.sha256" > "$receipt/inputs-after-check.log" 2>&1 || status=1
snapshot_source > "$receipt/source-after.sha256" || status=1
snapshot_tools > "$receipt/tools-after.tsv" || status=1
git diff --binary | sha256sum > "$receipt/freeze-after.sha256"
git status --porcelain > "$receipt/status-after.txt"
cmp "$receipt/source-before.sha256" "$receipt/source-after.sha256" || status=1
cmp "$receipt/tools-before.tsv" "$receipt/tools-after.tsv" || status=1
cmp "$receipt/freeze-before.sha256" "$receipt/freeze-after.sha256" || status=1
cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || status=1
find "$stage" -type f -print | LC_ALL=C sort > "$receipt/stage-files.txt"
printf '%s\n' "$status" > "$receipt/aggregate.status"
printf 'ACTUAL_HOST_AGGREGATE=%s\n' "$status"
exit "$status"
