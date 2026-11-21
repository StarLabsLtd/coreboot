#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

unset GIT_INDEX_FILE
cd "$(dirname "$0")"
source=ediie
remote=https://github.com/StarLabsLtd/edIIe.git
ref=${1:-refs/heads/main}
case "$ref" in
	refs/heads/*|refs/tags/*) git check-ref-format "$ref" ;;
	*) echo 'Specify a full branch or tag ref' >&2; exit 1 ;;
esac

if [ ! -e "$source/.git" ]; then
	# Git accepts a missing or empty destination, but will not replace files.
	git clone --no-checkout --no-tags "$remote" "$source"
elif [ ! -e "$source/.git" ] ||
     [ "$(git -C "$source" config --get remote.origin.url)" != "$remote" ] ||
     [ -n "$(git -C "$source" status --porcelain --ignore-submodules=none)" ]; then
	echo 'Refusing to replace an unrelated or modified edIIe checkout' >&2
	exit 1
fi

git -C "$source" fetch --no-tags origin "$ref"
commit=$(git -C "$source" rev-parse --verify 'FETCH_HEAD^{commit}')
if ! git -C "$source" symbolic-ref -q HEAD >/dev/null; then
	current=$(git -C "$source" rev-parse HEAD)
	prepared=$(git -C "$source" rev-parse --verify -q refs/ediie/prepared || :)
	if [ "$current" != "$prepared" ] &&
	   ! git -C "$source" merge-base --is-ancestor "$current" "$commit"; then
		echo 'Retain the detached edIIe revision on a branch before preparing another revision' >&2
		exit 1
	fi
fi
git -C "$source" checkout --detach "$commit"
git -C "$source" submodule update --init --checkout --recursive
git -C "$source" update-ref refs/ediie/prepared "$commit"
printf 'edIIe %s resolved to %s\n' "$ref" "$commit"
