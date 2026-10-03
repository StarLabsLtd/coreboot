#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
stage=/home/sean/pci-bus-mtrr-audit-packet.FvFYLO
packet="$stage/pci-bus-mtrr-audit-after615"
current=/home/sean/pci-bus-mtrr-audit-after614.kFd3bH
failed=/home/sean/native-private-auth2-after610.FeimaI
source=/home/sean/Documents/.cdk2-worktrees/pci-bus-mtrr-displacement-after614
head=90a8821c5e9a99d4bb4768268d1a0cda9fe3edf7
mkdir -p "$packet/fresh" "$packet/original-artifact" "$packet/failed-auth2-build"
for name in run.sh build.log build.time outer.status source-base.txt producer-head.txt \
	bearssl-head.txt source.patch source-after.patch source-before.sha256 \
	source-after.sha256 source-comparison.log patch-comparison.log \
	inputs-before.sha256 inputs-after-check.log outputs.sha256 resolved.config \
	checkpatch.log; do
	cp "$current/$name" "$packet/fresh/$name"
done
cp "$current/build/include/cdk2/config.h" "$packet/fresh/config.h"
cp "$current/build/native/direct-mtrr-ownership-inputs" "$packet/fresh/direct-mtrr-ownership-inputs"
cp "$current/build/native/native-direct-images.rsp" "$packet/fresh/native-direct-images.rsp"
for name in audit.sh log time before.sha256 after-check.log manifest; do
	case "$name" in
	audit.sh) original=original-artifact-audit.sh ;;
	manifest) original=original-artifact-manifest ;;
	*) original="original-artifact.$name" ;;
	esac
	if test "$name" = before.sha256; then original=original-artifact-before.sha256; fi
	if test "$name" = after-check.log; then original=original-artifact-after-check.log; fi
	cp "$current/$original" "$packet/original-artifact/$name"
done
for name in run.sh native.log native.time outer.status consumer-base.txt producer-head.txt \
	source-before.sha256 source-after-check.log inputs-before.sha256 inputs-after-check.log \
	codec-dependencies.make codec-dependencies.txt codec-closure-before.sha256 codec-after-check.log; do
	cp "$failed/$name" "$packet/failed-auth2-build/$name"
done
cp "$failed/artifacts/run.ZKioUc/build.log" "$packet/failed-auth2-build/build.log"
cp "$failed/artifacts/run.ZKioUc/resolved-payload.config" "$packet/failed-auth2-build/resolved-payload.config"
cp "$failed/artifacts/run.ZKioUc/source-before.sha256" "$packet/failed-auth2-build/copied-source-before.sha256"
git -C "$source" archive "$head" Makefile src/boot/Makefile \
	tests/direct_mtrr_manifest.sh tests/mtrr_ownership_test.sh \
	tests/direct_mtrr_ownership_test.sh tests/pe_exec_sections_fixture.c \
	util/pe_exec_sections.c | zstd -q -o "$packet/signed-audit-sources.tar.zst"
for path in Makefile src/boot/Makefile tests/direct_mtrr_manifest.sh \
	tests/mtrr_ownership_test.sh tests/direct_mtrr_ownership_test.sh \
	tests/pe_exec_sections_fixture.c util/pe_exec_sections.c; do
	blob=$(git -C "$source" rev-parse "$head:$path")
	digest=$(git -C "$source" show "$head:$path" | sha256sum)
	printf '%s\t%s\t%s\n' "$path" "$blob" "${digest%% *}"
done > "$packet/signed-audit-sources.tsv"
git -C "$source" log -1 --format='%H %P %G? %s' "$head" > "$packet/signed-source.txt"
cp "$stage/prepare.sh" "$packet/prepare.sh"
