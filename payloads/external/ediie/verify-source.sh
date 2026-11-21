#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

source=$1
if [ ! -e "$source/.git" ]; then
	echo 'Prepare edIIe: sh payloads/external/ediie/prepare-source.sh' >&2
	exit 1
fi
payload_git()
(
	# An alternate producer index does not describe the payload checkout.
	unset GIT_INDEX_FILE
	git -C "$source" "$@"
)

actual=$(payload_git rev-parse HEAD)
expected=$(payload_git rev-parse --verify refs/ediie/prepared) || {
	echo 'Prepare edIIe: sh payloads/external/ediie/prepare-source.sh' >&2
	exit 1
}
status=$(payload_git status --porcelain --untracked-files=normal --ignore-submodules=none)
if [ "$actual" != "$expected" ] || [ -n "$status" ]; then
	echo 'edIIe must match the clean revision selected during source preparation' >&2
	exit 1
fi
submodules=$(payload_git submodule status --recursive)
if printf '%s\n' "$submodules" | grep -Eq '^[+U-]'; then
	echo 'Re-run edIIe source preparation to initialize dependencies' >&2
	exit 1
fi
