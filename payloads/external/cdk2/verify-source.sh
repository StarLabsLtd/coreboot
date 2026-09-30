#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 submodule-path" >&2
	exit 2
fi

source_path=$1
expected=$(git rev-parse --verify ":$source_path") || {
	echo "CDK2 source has no staged gitlink" >&2
	exit 1
}
actual=$(git -C "$source_path" rev-parse HEAD)

if [ "$expected" != "$actual" ] ||
	[ -n "$(git -C "$source_path" status --porcelain \
		--untracked-files=normal --ignore-submodules=none)" ]; then
	echo 'CDK2 source must match the clean, staged coreboot gitlink' >&2
	exit 1
fi
