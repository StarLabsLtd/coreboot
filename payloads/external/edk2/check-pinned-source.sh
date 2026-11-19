#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

source=$1
repository=$2
revision=$3
allowed_dirty=${4:-}

fail()
{
	echo "EDK2 source check: $*" >&2
	exit 1
}

[ -n "$repository" ] && [ -n "$revision" ] ||
	fail "repository URL and revision are required"

if [ ! -d "$source" ]; then
	git clone -- "$repository" "$source"
fi
cd "$source"
[ "$(git rev-parse --show-toplevel)" = "$(pwd -P)" ] ||
	fail "$source is not the root of a Git checkout"
[ "$(git config --get remote.origin.url)" = "$repository" ] ||
	fail "$source belongs to a different repository; move it out of the workspace"

# An immutable cached pin needs no refresh. Symbolic refs must be fetched.
case "$revision" in
	*[!0-9a-fA-F]*) pinned=no ;;
	*) if [ "${#revision}" -eq 40 ]; then pinned=yes; else pinned=no; fi ;;
esac
if [ "$pinned" = no ] || ! git cat-file -e "${revision}^{commit}" 2>/dev/null; then
	git fetch --no-recurse-submodules origin
fi
requested=$(git rev-parse --verify -q --end-of-options "${revision}^{commit}") ||
	fail "$revision is not a commit"

# Ignore untracked build outputs. The configured splash may be regenerated
# at the same revision, but must never hide unrelated edits.
dirty=$(git status --porcelain --untracked-files=no --ignore-submodules=untracked)
if [ -n "$dirty" ]; then
	[ -n "$allowed_dirty" ] && [ "$(git rev-parse HEAD)" = "$requested" ] ||
		fail "tracked changes in $source"
	dirty=$(git status --porcelain --untracked-files=no --ignore-submodules=untracked -- \
		. ":(exclude,literal)$allowed_dirty")
	[ -z "$dirty" ] || fail "tracked changes in $source: $dirty"
else
	git -c submodule.recurse=false checkout --detach "$requested"
fi

# Synchronize each parent after it reaches its requested revision.
git submodule sync
git -c submodule.recurse=false -c fetch.recurseSubmodules=false submodule update --init --checkout
git submodule foreach --recursive '
	git submodule sync &&
	git -c submodule.recurse=false -c fetch.recurseSubmodules=false submodule update --init --checkout
'

[ "$(git rev-parse HEAD)" = "$requested" ] ||
	fail "checked out revision does not match $revision"
submodules=$(git submodule status --recursive)
if printf '%s\n' "$submodules" | grep -Eq '^[+-U]'; then
	fail "submodule revision does not match the checked out source"
fi
git submodule foreach --recursive 'git diff --quiet HEAD --' >/dev/null ||
	fail "tracked submodule source is modified"
