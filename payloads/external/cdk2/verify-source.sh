#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
	echo "usage: $0 submodule-path [development-mode]" >&2
	exit 2
fi

source_path=$(realpath --relative-to="$(git rev-parse --show-toplevel)" "$1")
development=0
case ${2:-} in
	''|0) ;;
	1) development=1 ;;
	*) echo 'CDK2_DEVELOPMENT must be 0 or 1' >&2; exit 2 ;;
esac
if [ ! -e "$source_path/.git" ]; then
	echo 'Initialize the pinned CDK2 submodule before configuring or building' >&2
	exit 1
fi
expected=$(git rev-parse --verify ":$source_path") || {
	echo "CDK2 source has no staged gitlink" >&2
	exit 1
}
actual=$(git -C "$source_path" rev-parse HEAD)

if [ "${development:-0}" = 1 ]; then
	echo "CDK2 development checkout: $actual (recorded: $expected)" >&2
	exit 0
fi

source_status=$(git -C "$source_path" status --porcelain \
	--untracked-files=normal --ignore-submodules=none) || {
	echo 'Cannot inspect CDK2 source changes' >&2
	exit 1
}
if [ "$expected" != "$actual" ] || [ -n "$source_status" ]; then
	echo 'CDK2 source must match the clean, staged coreboot gitlink' >&2
	exit 1
fi
