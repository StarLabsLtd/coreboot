#!/bin/bash
set -euo pipefail
test "$#" = 1 || { echo 'usage: run.sh NEW_ABSOLUTE_RECEIPT_DIRECTORY' >&2; exit 2; }
test -z "${PYTHONOPTIMIZE+x}"
test -z "$(env | sed -n '/^CDK2_/p')"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
export MAKEFLAGS= MFLAGS= MAKEOVERRIDES= HOSTCC=cc CC=cc COREBOOT_CONFIG=
canonical=/home/sean/Documents/cdk2
expected=6faf053c28d7b33de30daeaeae8408340685d006
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=$1
case "$out" in /home/sean/*) ;; *) echo 'absolute new host receipt required' >&2; exit 2;; esac
test ! -e "$out"
mkdir "$out"
out=$(realpath "$out")
fw=/home/sean/Documents/.cdk2-worktrees/full-lint-after661-host
build=$out/build
export TMPDIR=$out/tmp
mkdir "$TMPDIR"
printf '1\n' > "$out/aggregate.status"
test "$(git -C "$canonical" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$canonical" status --porcelain --untracked-files=all)"
sha256sum "$recipe"/* > "$out/recipe-before.sha256"
stage() {
	local label=$1 status=0; shift
	python3 -c 'import json,os,sys; print(json.dumps({"argv":sys.argv[1:],"environment":dict(sorted(os.environ.items()))},indent=2))' "$@" > "$out/$label.argv.json"
	/usr/bin/time -p -o "$out/$label.time" "$@" > "$out/$label.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$out/$label.status"
	return "$status"
}
stage canonical-signature git -C "$canonical" verify-commit "$expected"
# Root prepares this clean exact-HEAD worktree and pinned vendors separately.
# Its private CWD keeps the unchanged lint driver's .tmpconfig off canonical.
test -d "$fw"
test "$(git -C "$fw" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$fw" status --porcelain --untracked-files=all)"
stage private-signature git -C "$fw" verify-commit "$expected"
for vendor in lvgl bearssl; do
	expected_vendor=$(git -C "$canonical" ls-tree "$expected" "3rdparty/$vendor" | awk '{print $3}')
	test "$(git -C "$canonical/3rdparty/$vendor" rev-parse HEAD)" = "$expected_vendor"
	test -z "$(git -C "$canonical/3rdparty/$vendor" status --porcelain --untracked-files=all)"
	test "$(git -C "$fw/3rdparty/$vendor" rev-parse HEAD)" = "$expected_vendor"
	test -z "$(git -C "$fw/3rdparty/$vendor" status --porcelain --untracked-files=all)"
done
inputs=("$canonical" "$fw" "$canonical/3rdparty/lvgl" "$canonical/3rdparty/bearssl" "$fw/3rdparty/lvgl" "$fw/3rdparty/bearssl")
tool_names=(bash sh python3 git ssh-keygen make cc gcc ld as ar perl kconfig-conf awk sed grep sha256sum stat cp cmp mktemp rm dirname basename sort readlink realpath cut mkdir env uname getopt tee cat wc tr uniq xargs find mv flock head)
for tool in "${tool_names[@]}"; do
	path=$(command -v "$tool")
	printf '%s\t%s\n' "$tool" "$path" >> "$out/tools-resolved.tsv"
	inputs+=("$path")
done
inputs+=(/usr/bin/time)
for support in cc1 as ld collect2 lto-wrapper; do
	path=$(cc "-print-prog-name=$support")
	test -f "$path" || path=$(command -v "$path")
	printf '%s\t%s\n' "$support" "$path" >> "$out/compiler-support.tsv"
	inputs+=("$path")
done
for support in libgcc.a liblto_plugin.so; do
	path=$(cc "-print-file-name=$support")
	test -f "$path"
	printf '%s\t%s\n' "$support" "$path" >> "$out/compiler-support.tsv"
	inputs+=("$path")
done
python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-before.json"
git -C "$canonical" rev-parse HEAD > "$out/canonical-head-before.txt"
git -C "$fw" diff --binary HEAD > "$out/source-before.diff"
execution=1
finish() {
	local original=$? closure=0 aggregate=0
	trap - EXIT
	set +e
	python3 "$recipe/hash-inputs.py" "${inputs[@]}" > "$out/inputs-after.json" || closure=1
	cmp "$out/inputs-before.json" "$out/inputs-after.json" > "$out/inputs-check.log" 2>&1 || closure=1
	git -C "$canonical" rev-parse HEAD > "$out/canonical-head-after.txt"
	cmp "$out/canonical-head-before.txt" "$out/canonical-head-after.txt" > "$out/head-check.log" 2>&1 || closure=1
	git -C "$fw" diff --binary HEAD > "$out/source-after.diff"
	cmp "$out/source-before.diff" "$out/source-after.diff" > "$out/diff-check.log" 2>&1 || closure=1
	sha256sum -c "$out/recipe-before.sha256" > "$out/recipe-check.log" 2>&1 || closure=1
	if test -f "$out/config-before.sha256"; then
		sha256sum -c "$out/config-before.sha256" > "$out/config-check.log" 2>&1 || closure=1
	fi
	find "$fw" -maxdepth 1 -name '.tmpconfig.lint*' -print > "$out/lint-transient-leftovers.txt"
	test ! -s "$out/lint-transient-leftovers.txt" || closure=1
	printf '%s\n' "$execution" > "$out/execution.status"
	printf '%s\n' "$closure" > "$out/closure.status"
	test "$execution" = 0 && test "$closure" = 0 && test "$original" = 0 || aggregate=1
	printf '%s\n' "$aggregate" > "$out/aggregate.status"
	exit "$aggregate"
}
trap finish EXIT
common=(make -C "$fw" -j1 "CDK2_BUILD_DIR=$build" COREBOOT_CONFIG= CC=cc HOSTCC=cc)
stage configure "${common[@]}" defconfig
test -f "$build/kconfig/coreboot-source.tmp"
test ! -s "$build/kconfig/coreboot-source.tmp"
test "$(head -n 1 "$build/kconfig/coreboot-input.identity")" = standalone
sha256sum "$build/.config" "$build/include/cdk2/config.h" "$build/kconfig/coreboot-source.tmp" "$build/kconfig/coreboot-input.identity" > "$out/config-before.sha256"
find "$fw/util/lint" -maxdepth 1 -type f -name 'lint-stable-*' -print | sort > "$out/stable-script-inventory.txt"
find "$fw/util/lint" -maxdepth 1 -type f -name 'lint-[0-9]*' -print | sort > "$out/full-script-inventory.txt"
find "$fw/util/lint" -maxdepth 1 -type f -name 'lint-extended-*' -print | sort > "$out/extended-script-inventory.txt"
wc -l "$out/stable-script-inventory.txt" "$out/full-script-inventory.txt" "$out/extended-script-inventory.txt" > "$out/script-counts.txt"
git -C "$fw" ls-files Kconfig src include util tests | grep '.*\.[ch]$\|Kconfig.*$' | grep -v '^payloads/libpayload/util/kconfig\|^payloads/libpayload/curses/PDCurses\|^src/vendorcode/wuffs\|^util/coreboot-configurator\|^util/crossgcc/patches\|^util/inteltool\|^util/kconfig\|^util/superiotool\|^Documentation' > "$out/full-checkpatch-inputs.txt"
# Preserve every requested raw result even if one gate fails.
execution=0
stage lint "${common[@]}" CDK2_CONFIG_READY=1 lint || execution=1
stage lint-extended "${common[@]}" CDK2_CONFIG_READY=1 lint-extended || execution=1
cd "$fw"
stage full-checkpatch sh util/lint/lint-007-checkpatch || execution=1
