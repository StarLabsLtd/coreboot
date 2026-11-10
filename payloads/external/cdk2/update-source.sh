#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

source_path=payloads/external/cdk2/cdk2
if [ ! -e "$source_path/.git" ]; then
	git submodule update --init --checkout -- "$source_path"
fi
if [ -n "$(git -C "$source_path" status --porcelain --untracked-files=normal --ignore-submodules=none)" ]; then
	echo 'CDK2 source has local changes' >&2
	exit 1
fi
git -C "$source_path" fetch --quiet origin main
git -C "$source_path" checkout --quiet --detach FETCH_HEAD
git -C "$source_path" submodule update --init --recursive
