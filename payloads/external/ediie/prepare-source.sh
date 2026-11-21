#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

unset GIT_INDEX_FILE
if [ "${1:-}" = --local ]; then
	test "$#" -eq 2 || { echo 'Usage: prepare-source.sh --local <checkout>' >&2; exit 1; }
	exec sh "$(dirname "$0")/verify-source.sh" "$2" --record-local
fi
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
	git clone --no-tags "$remote" "$source"
elif [ "$(git -C "$source" config --get remote.origin.url)" != "$remote" ]; then
	echo 'Refusing to replace an unrelated edIIe checkout' >&2
	exit 1
fi

current=$(git -C "$source" rev-parse HEAD)
pending=$(git -C "$source" rev-parse --verify -q refs/ediie/preparing || :)
dirty=$(git -C "$source" status --porcelain --ignore-submodules=none)
if [ -n "$dirty" ]; then
	# Retry an interrupted dependency update, not an edited checkout.
	if [ "$current" != "$pending" ] ||
	   [ -n "$(git -C "$source" status --porcelain --ignore-submodules=all)" ] ||
	   ! git -C "$source" diff --cached --quiet HEAD --ignore-submodules=none ||
	   ! git -C "$source" submodule foreach --quiet --recursive '
		test -z "$(git status --porcelain --ignore-submodules=all)" &&
		git diff --cached --quiet HEAD --ignore-submodules=none
	   '; then
		echo 'Refusing to replace a modified edIIe checkout' >&2
		exit 1
	fi
	# A clean dependency may still contain a locally committed detached HEAD.
	git -C "$source" submodule foreach --quiet --recursive '
		head=$(git rev-parse HEAD) &&
		if [ "$head" != "$sha1" ] && ! git symbolic-ref -q HEAD >/dev/null; then
			git update-ref "refs/ediie/retained/$head" "$head"
		fi
	'
fi

git -C "$source" fetch --no-tags --no-recurse-submodules origin "$ref"
commit=$(git -C "$source" rev-parse --verify 'FETCH_HEAD^{commit}')
if ! git -C "$source" symbolic-ref -q HEAD >/dev/null; then
	prepared=$(git -C "$source" rev-parse --verify -q refs/ediie/prepared || :)
	if [ "$current" != "$prepared" ] && [ "$current" != "$pending" ] &&
	   ! git -C "$source" merge-base --is-ancestor "$current" "$commit"; then
		echo 'Retain the detached edIIe revision on a branch before preparing another revision' >&2
		exit 1
	fi
fi
git -C "$source" update-ref refs/ediie/preparing "$commit"
git -C "$source" -c submodule.recurse=false checkout --detach "$commit"
git -C "$source" submodule update --init --checkout --recursive
git -C "$source" update-ref refs/ediie/prepared "$commit"
git -C "$source" update-ref -d refs/ediie/preparing "$commit"
printf 'edIIe %s resolved to %s\n' "$ref" "$commit"
