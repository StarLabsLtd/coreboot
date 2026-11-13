#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

source=$1
revision=$2
allowed_dirty=${3:-}

fail()
{
	echo "EDK2 release source check: $*" >&2
	exit 1
}

# git diff uses 1 for differences and other nonzero codes for errors. Exclude
# exactly the one generated tracked asset, without parsing pathnames as words.
clean_except_asset()
{
	if [ -n "$allowed_dirty" ]; then
		git diff --quiet HEAD -- . ":(exclude,literal)$allowed_dirty" && return 0
	else
		git diff --quiet HEAD -- && return 0
	fi
	status=$?
	[ "$status" -eq 1 ] && return 1
	fail "unable to inspect tracked source (git diff status $status)"
}

cd "$source"
requested=$(git rev-parse --verify -q "${revision}^{commit}") ||
	fail "${revision} is not a commit"
head=$(git rev-parse HEAD) || fail "unable to identify source revision"

clean_except_asset || fail "tracked source is modified"
if git diff --quiet HEAD --; then
	git checkout --detach "$requested"
	git submodule update --init --checkout --recursive
else
	status=$?
	[ "$status" -eq 1 ] || fail "unable to inspect tracked source (git diff status $status)"
	[ "$head" = "$requested" ] ||
		fail "a modified source tree is not at the requested revision"
fi

[ "$(git rev-parse HEAD)" = "$requested" ] ||
	fail "checked out revision does not match the requested commit"
clean_except_asset || fail "tracked source is modified"

submodules=$(git submodule status --recursive) ||
	fail "unable to inspect submodule revisions"
if printf '%s\n' "$submodules" | grep -Eq '^[+-U]'; then
	fail "submodule revision does not match the checked out source"
fi
git submodule foreach --recursive 'git diff --quiet HEAD -- || exit 1' >/dev/null ||
	fail "tracked submodule source is modified or unavailable"
