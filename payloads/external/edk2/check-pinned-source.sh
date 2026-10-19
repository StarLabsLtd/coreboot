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

cd "$source"
requested=$(git rev-parse --verify -q "${revision}^{commit}") ||
	fail "${revision} is not a commit"
head=$(git rev-parse HEAD)
dirty=$(git diff --name-only HEAD --)

if [ -n "$dirty" ]; then
	for path in $dirty; do
		[ "$path" = "$allowed_dirty" ] ||
			fail "tracked source is modified: $path"
	done
	[ "$head" = "$requested" ] ||
		fail "a modified source tree is not at the requested revision"
else
	git checkout --detach "$requested"
	git submodule update --init --checkout --recursive
	fi

[ "$(git rev-parse HEAD)" = "$requested" ] ||
	fail "checked out revision does not match the requested commit"

dirty=$(git diff --name-only HEAD --)
if [ -n "$dirty" ]; then
	for path in $dirty; do
		[ "$path" = "$allowed_dirty" ] ||
			fail "tracked source is modified: $path"
	done
fi

git submodule status --recursive | grep -Eq '^[+-U]' &&
	fail "submodule revision does not match the checked out source"
git submodule foreach --recursive 'git diff --quiet HEAD -- || exit 1' >/dev/null ||
	fail "tracked submodule source is modified"
