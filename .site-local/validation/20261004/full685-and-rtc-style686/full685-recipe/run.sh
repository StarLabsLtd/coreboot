#!/bin/bash
set -euo pipefail
test "$#" = 2 || { echo 'usage: run.sh NEW_ABSOLUTE_RECEIPT_DIRECTORY PUBLISHED_SIGNED_HEAD' >&2; exit 2; }
test -z "${PYTHONOPTIMIZE+x}"
test -z "$(env | sed -n '/^CDK2_/p')"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=/home/sean LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1
export MAKEFLAGS= MFLAGS= MAKEOVERRIDES= HOSTCC=cc CC=cc
canonical=/home/sean/Documents/cdk2
expected=$2
[[ $expected =~ ^[0-9a-f]{40}$ ]]
test "$expected" = ddc179372681983e2f903ab0bb4cb7b0f0c495ff
candidate=/home/sean/Documents/.cdk2-worktrees/roadmap-checkpoint-after675
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
normal=/home/sean/fresh-normal-focus-after657.CftZZg/build
input=/home/sean/native-lvgl-normal-after623.zLwvcS/resolved.config
producer_config=/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
out=$1
case "$out" in /home/sean/*) ;; *) echo 'absolute new host receipt required' >&2; exit 2;; esac
test ! -e "$out"
mkdir "$out"
out=$(realpath "$out")
fw=/home/sean/Documents/.cdk2-worktrees/full-lint-after685-host
build=$out/build
export TMPDIR=$out/tmp
mkdir "$TMPDIR"
printf '1\n' > "$out/aggregate.status"
test "$(git -C "$canonical" rev-parse HEAD)" = a3ed5f42934cf4ff7f8171ba5a709c2c31f801a6
test -z "$(git -C "$canonical" status --porcelain --untracked-files=all)"
test "$(git -C "$candidate" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$candidate" status --porcelain --untracked-files=all)"
sha256sum "$recipe"/* > "$out/recipe-before.sha256"
stage() {
	local label=$1 status=0; shift
	python3 -c 'import json,os,sys; print(json.dumps({"argv":sys.argv[1:],"environment":dict(sorted(os.environ.items()))},indent=2))' "$@" > "$out/$label.argv.json"
	/usr/bin/time -p -o "$out/$label.time" "$@" > "$out/$label.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$out/$label.status"
	return "$status"
}
stage canonical-signature git -C "$canonical" verify-commit HEAD
stage candidate-signature git -C "$candidate" verify-commit "$expected"
# Root prepares this clean exact-HEAD worktree and pinned vendors separately.
# Its private CWD keeps the unchanged lint driver's .tmpconfig off canonical.
test -d "$fw"
test "$(git -C "$fw" rev-parse HEAD)" = "$expected"
test -z "$(git -C "$fw" status --porcelain --untracked-files=all)"
stage private-signature git -C "$fw" verify-commit "$expected"
for vendor in lvgl bearssl; do
	expected_vendor=$(git -C "$candidate" ls-tree "$expected" "3rdparty/$vendor" | awk '{print $3}')
	test "$(git -C "$canonical/3rdparty/$vendor" rev-parse HEAD)" = "$expected_vendor"
	test -z "$(git -C "$canonical/3rdparty/$vendor" status --porcelain --untracked-files=all)"
	test "$(git -C "$fw/3rdparty/$vendor" rev-parse HEAD)" = "$expected_vendor"
	test -z "$(git -C "$fw/3rdparty/$vendor" status --porcelain --untracked-files=all)"
done
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain --untracked-files=all)"
stage producer-signature git -C "$producer" verify-commit HEAD
sha256sum --quiet -c <<'PINS'
4a2ee6d9fba3b30a8be8efbc4392eef077d61b15221e7ade60b1475a8a724258  /home/sean/native-lvgl-normal-after623.zLwvcS/resolved.config
d2f5db5a8d33dab72cca9587ca958e67c657b1d2ea0bec18ce69bff775bd1492  /home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config
ac668eb9c8a622e020182653fd832e6671741f5e616a01cfa4fb33b8e9b4065f  /home/sean/fresh-normal-focus-after657.CftZZg/build/include/cdk2/config.h
10f6aef02d7678a1ae8d5ffab8157b5ab0a37438576a645cdf237fe130fa2aac  /home/sean/fresh-normal-focus-after657.CftZZg/build/resolved.config
PINS
inputs=("$input" "$producer_config" "$normal/resolved.config" "$normal/include/cdk2/config.h" "$producer" "$canonical" "$candidate" "$fw" "$canonical/3rdparty/lvgl" "$canonical/3rdparty/bearssl" "$fw/3rdparty/lvgl" "$fw/3rdparty/bearssl")
tool_names=(bash sh python3 git ssh-keygen make cc gcc ld as ar perl kconfig-conf awk sed grep sha256sum stat cp cmp mktemp rm dirname basename sort readlink realpath cut mkdir env uname getopt tee cat wc tr uniq xargs find mv flock head ln)
paths()
{
    local tool path
    for tool in "${tool_names[@]}"; do
        path=$(type -P "$tool")
        printf '%s\t%s\t%s\n' "$tool" "$path" "$(readlink -f "$path")"
    done
}
paths > "$out/tools-resolved-before.tsv"
while IFS=$'\t' read -r tool path resolved; do inputs+=("$path"); done < "$out/tools-resolved-before.tsv"
config_state()
{
    local path
    for path in "$fw/.checkpatch.conf" /home/sean/.checkpatch.conf "$fw/.scripts/.checkpatch.conf"; do
        if test -e "$path" || test -L "$path"; then
            test -f "$path" && test ! -L "$path"
            sha256sum "$path"
        else
            printf 'absent %s\n' "$path"
        fi
    done
}
config_state > "$out/checker-config-before.txt"
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
	paths > "$out/tools-resolved-after.tsv" || closure=1
	cmp "$out/tools-resolved-before.tsv" "$out/tools-resolved-after.tsv" > "$out/tools-resolved-check.log" 2>&1 || closure=1
	config_state > "$out/checker-config-after.txt" || closure=1
	cmp "$out/checker-config-before.txt" "$out/checker-config-after.txt" > "$out/checker-config-check.log" 2>&1 || closure=1
	if test -f "$out/compiler-inputs-before.sha256"; then
		sha256sum -c "$out/compiler-inputs-before.sha256" > "$out/compiler-inputs-check.log" 2>&1 || closure=1
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
mkdir "$build"
cp "$input" "$build/.config"
common=(make -C "$fw" -j1 "CDK2_BUILD_DIR=$build" "CDK2_CONFIG=$build/.config"
    "COREBOOT_TREE=$producer" "COREBOOT_CONFIG=$producer_config" CC=cc HOSTCC=cc)
stage configure "${common[@]}" olddefconfig
stage config-match cmp "$normal/resolved.config" "$build/.config"
stage header-match cmp "$normal/include/cdk2/config.h" "$build/include/cdk2/config.h"
for flag in CDK2_COREBOOT_CAPSULE_PROFILE CDK2_NATIVE_SYSTEM_FMP PAYLOAD_DMA_HANDOFF; do
    grep -Fxq "#define CONFIG_$flag 1" "$build/include/cdk2/config.h"
done
sha256sum "$build/.config" "$build/include/cdk2/config.h" > "$out/config-before.sha256"
stage compiler-inputs gcc -std=c11 -M "$fw/util/lint/cdk2-checkpatch-filter.c"
perl -MText::ParseWords=shellwords -0777 -ne '
    s/\\\n/ /g;
    for (split /\n/) { s/^[^:]+:\s*// or next; s/\$\$/\$/g;
        print join("\n", shellwords($_)), "\n"; }
' "$out/compiler-inputs.log" | sort -u > "$out/compiler-inputs.txt"
test -s "$out/compiler-inputs.txt"
xargs -d '\n' sha256sum < "$out/compiler-inputs.txt" > "$out/compiler-inputs-before.sha256"
find "$fw/util/lint" -maxdepth 1 -type f -name 'lint-stable-*' -print | sort > "$out/stable-script-inventory.txt"
find "$fw/util/lint" -maxdepth 1 -type f -name 'lint-[0-9]*' -print | sort > "$out/full-script-inventory.txt"
find "$fw/util/lint" -maxdepth 1 -type f -name 'lint-extended-*' -print | sort > "$out/extended-script-inventory.txt"
wc -l "$out/stable-script-inventory.txt" "$out/full-script-inventory.txt" "$out/extended-script-inventory.txt" > "$out/script-counts.txt"
git -C "$fw" ls-files Kconfig src include util tests | grep '.*\.[ch]$\|Kconfig.*$' | grep -v '^payloads/libpayload/util/kconfig\|^payloads/libpayload/curses/PDCurses\|^src/vendorcode/wuffs\|^util/coreboot-configurator\|^util/crossgcc/patches\|^util/inteltool\|^util/kconfig\|^util/superiotool\|^Documentation' |
    grep -v '^src/lib/tcg_hash/vendor/vboot/2sha\(1\|256\|512\)\.c$' > "$out/full-checkpatch-inputs.txt"
# Preserve every requested raw result even if one gate fails.
execution=0
stage lint "${common[@]}" lint || execution=1
stage lint-extended "${common[@]}" lint-extended || execution=1
cd "$fw"
stage full-checkpatch sh util/lint/lint-007-checkpatch || execution=1
