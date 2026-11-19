#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

source=$1
expected=$(git ls-files --stage -- "$source" | awk '$1 == "160000" { print $2 }')
if [ -z "$expected" ] || [ ! -e "$source/.git" ]; then
	echo 'Initialize edIIe: git submodule update --init --checkout --recursive -- payloads/external/ediie/ediie' >&2
	exit 1
fi
actual=$(git -C "$source" rev-parse HEAD)
status=$(git -C "$source" status --porcelain --untracked-files=normal --ignore-submodules=none)
if [ "$actual" != "$expected" ] || [ -n "$status" ]; then
	echo 'edIIe must match the clean revision recorded by coreboot' >&2
	exit 1
fi
submodules=$(git -C "$source" submodule status --recursive)
if printf '%s\n' "$submodules" | grep -Eq '^[+U-]'; then
	echo 'Initialize edIIe dependencies: git submodule update --init --checkout --recursive -- payloads/external/ediie/ediie' >&2
	exit 1
fi
