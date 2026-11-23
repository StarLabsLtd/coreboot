#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

# The payload checkout has its own index.
unset GIT_INDEX_FILE

source=$1
repository=$2
revision=$3
allowed_dirty=${4:-}
scope=${5:-payload}

fail()
{
	echo "EDK2 source check: $*" >&2
	exit 1
}

[ -n "$repository" ] && [ -n "$revision" ] ||
	fail "repository URL and revision are required"

if [ ! -d "$source" ]; then
	[ "$scope" != verify ] || fail "prepare $source before building"
	git clone -- "$repository" "$source"
fi
cd "$source"
[ "$(git rev-parse --show-toplevel)" = "$(pwd -P)" ] ||
	fail "$source is not the root of a Git checkout"
[ "$(git config --get remote.origin.url)" = "$repository" ] ||
	fail "$source belongs to a different repository; move it out of the workspace"

if [ "$scope" = verify ]; then
	[ "$(git config --get coreboot.prepared-source)" = "$revision" ] &&
	[ "$(git config --get coreboot.prepared-commit)" = "$(git rev-parse HEAD)" ] ||
		fail "prepare $source for $revision before building"
	set -- .
	if [ -n "$allowed_dirty" ]; then
		set -- . ":(exclude,literal)$allowed_dirty"
	fi
	dirty=$(git status --porcelain --untracked-files=no --ignore-submodules=untracked -- "$@")
	[ -z "$dirty" ] || fail "tracked changes in $source: $dirty"
	submodules=$(git submodule status --recursive)
	if printf '%s\n' "$submodules" | grep -Eq '^[+-U]'; then
		fail "prepare $source to initialize its dependencies"
	fi
	exit 0
fi

# Refresh branches and tags during explicit preparation. Cached commit objects work offline.
case "$revision" in
	*[!0-9a-fA-F]*) pinned=no ;;
	*) if [ "${#revision}" -eq 40 ]; then pinned=yes; else pinned=no; fi ;;
esac
resolve=$revision
case "$revision" in
origin/HEAD)
	git fetch --no-tags --no-recurse-submodules origin HEAD
	resolve=FETCH_HEAD
	;;
refs/heads/*)
	git check-ref-format "$revision" || fail "invalid branch ref: $revision"
	git fetch --no-tags --no-recurse-submodules origin "$revision"
	resolve=FETCH_HEAD
	;;
origin/*)
	branch="refs/heads/${revision#origin/}"
	tracking="refs/remotes/$revision"
	git check-ref-format "$branch" || fail "invalid branch ref: $revision"
	git fetch --no-tags --no-recurse-submodules origin "+$branch:$tracking"
	resolve=$tracking
	;;
refs/tags/*)
	git check-ref-format "$revision" || fail "invalid tag ref: $revision"
	git fetch --no-tags --no-recurse-submodules origin "$revision"
	resolve=FETCH_HEAD
	;;
*)
	if [ "$pinned" = no ] || ! git cat-file -e "${revision}^{commit}" 2>/dev/null; then
		git fetch --no-recurse-submodules origin
	fi
	;;
esac
requested=$(git rev-parse --verify -q --end-of-options "${resolve}^{commit}") ||
	fail "$revision is not a commit"

# Ignore untracked build outputs. The configured splash may be regenerated
# at the same revision, but must never hide unrelated edits.
previous=$(git rev-parse HEAD)
dirty=$(git status --porcelain --untracked-files=no --ignore-submodules=untracked)
if [ -n "$dirty" ]; then
	[ -n "$allowed_dirty" ] && [ "$previous" = "$requested" ] ||
		fail "tracked changes in $source"
	dirty=$(git status --porcelain --untracked-files=no --ignore-submodules=untracked -- . ":(exclude,literal)$allowed_dirty")
	[ -z "$dirty" ] || fail "tracked changes in $source: $dirty"
else
	git -c submodule.recurse=false checkout --detach "$requested"
fi

# Synchronize each parent after it reaches its requested revision.
update_submodules()
{
	git submodule sync &&
	git -c submodule.recurse=false -c fetch.recurseSubmodules=false \
		submodule update --init --checkout &&
	git submodule foreach --recursive '
		git submodule sync &&
		git -c submodule.recurse=false -c fetch.recurseSubmodules=false \
			submodule update --init --checkout
	'
}

if ! update_submodules; then
	if [ "$previous" != "$requested" ]; then
		if ! git -c submodule.recurse=false checkout --detach "$previous" ||
		   ! update_submodules; then
			fail "could not restore $previous after updating submodules"
		fi
	fi
	fail "could not prepare submodules for $requested"
fi

[ "$(git rev-parse HEAD)" = "$requested" ] ||
	fail "checked out revision does not match $revision"
submodules=$(git submodule status --recursive)
if printf '%s\n' "$submodules" | grep -Eq '^[+-U]'; then
	fail "submodule revision does not match the checked out source"
fi
git submodule foreach --recursive 'git diff --quiet HEAD --' >/dev/null ||
	fail "tracked submodule source is modified"
git config coreboot.prepared-source "$revision"
git config coreboot.prepared-commit "$requested"
printf 'EDK2 %s resolved to %s\n' "$revision" "$requested"
