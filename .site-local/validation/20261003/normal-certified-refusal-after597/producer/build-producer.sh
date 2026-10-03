#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

task_core=${1:?actual freshly built normal Core ELF required}
task_output=${2:?new owned build output directory required}
task_version=${3:?firmware version required}
task_source=/home/sean/Documents/.coreboot-worktrees/capsule-signature-refusal-after386

test -f "$task_core"
test ! -e "$task_output"
case "$task_version" in
0x001a0009|0x001a000a) ;;
*) exit 2 ;;
esac
mkdir "$task_output"
cp /home/sean/signature-refusal-selected-peer.SMoS73/full.config "$task_output/full.config"
cd "$task_source"
git rev-parse HEAD > "$task_output/source-head.txt"
git status --porcelain > "$task_output/source-status.txt"
test ! -s "$task_output/source-status.txt"
sha256sum "$task_core" > "$task_output/core-before.sha256"
util/scripts/config --file "$task_output/full.config" \
	--set-str PAYLOAD_FILE "$task_core" \
	--set-val DRIVERS_EFI_MAIN_FW_VERSION "$task_version"
cp "$task_output/full.config" "$task_output/input.config"
PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export PATH
make -j2 UPDATED_SUBMODULES=1 obj="$task_output/build" \
	DOTCONFIG="$task_output/full.config" \
	VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot \
	olddefconfig > "$task_output/configure.log" 2>&1
sha256sum "$task_output/full.config" > "$task_output/resolved-before.sha256"
/usr/bin/time -o "$task_output/build.time" \
	make -j2 UPDATED_SUBMODULES=1 obj="$task_output/build" \
	DOTCONFIG="$task_output/full.config" \
	VBOOT_SOURCE=/home/sean/Documents/coreboot/3rdparty/vboot \
	all > "$task_output/build.log" 2>&1
sha256sum -c "$task_output/core-before.sha256"
sha256sum -c "$task_output/resolved-before.sha256"
sha256sum "$task_output/build/coreboot.rom" \
	"$task_output/build/util/cbfstool/cbfstool" > "$task_output/output.sha256"
