#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

# This checker owns a separate repository, not the producer's index.
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
previous=$(git rev-parse HEAD)
if [ "$scope" = fixed-mm ]; then
	# An interrupted update may leave clean submodules at older commits.
	git diff --quiet HEAD --ignore-submodules=all -- &&
	git diff --cached --quiet HEAD --ignore-submodules=none -- &&
	git submodule foreach --quiet --recursive '
		git diff --quiet HEAD --ignore-submodules=all -- &&
		git diff --cached --quiet HEAD --ignore-submodules=none --
	' || fail "tracked changes in $source or its submodules"
	dirty=
else
	dirty=$(git status --porcelain --untracked-files=no --ignore-submodules=untracked)
fi
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
	if [ "$scope" = fixed-mm ]; then
		git submodule sync -- "$@" &&
		git -c submodule.recurse=false -c fetch.recurseSubmodules=false \
			submodule update --init --checkout -- "$@"
		return
	fi
	git submodule sync &&
	git -c submodule.recurse=false -c fetch.recurseSubmodules=false \
		submodule update --init --checkout &&
	git submodule foreach --recursive '
		git submodule sync &&
		git -c submodule.recurse=false -c fetch.recurseSubmodules=false \
			submodule update --init --checkout
	'
}

if [ "$scope" = fixed-mm ]; then
	# BaseTools examines include paths even in unselected packages.
	set -- BaseTools/Source/C/BrotliCompress/brotli \
		MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
		MdePkg/Library/MipiSysTLib/mipisyst \
		CryptoPkg/Library/MbedTlsLib/mbedtls \
		SecurityPkg/DeviceSecurity/SpdmLib/libspdm \
		CryptoPkg/Library/OpensslLib/openssl
fi

if ! update_submodules "$@"; then
	if [ "$previous" != "$requested" ]; then
		if ! git -c submodule.recurse=false checkout --detach "$previous" ||
		   ! update_submodules "$@"; then
			fail "could not restore $previous after updating submodules"
		fi
	fi
	fail "could not prepare submodules for $requested"
fi

[ "$(git rev-parse HEAD)" = "$requested" ] ||
	fail "checked out revision does not match $revision"
if [ "$scope" = fixed-mm ]; then
	submodules=$(git submodule status -- "$@")
else
	submodules=$(git submodule status --recursive)
fi
if printf '%s\n' "$submodules" | grep -Eq '^[+-U]'; then
	fail "submodule revision does not match the checked out source"
fi
if [ "$scope" != fixed-mm ]; then
	git submodule foreach --recursive 'git diff --quiet HEAD --' >/dev/null ||
		fail "tracked submodule source is modified"
	git config coreboot.prepared-source "$revision"
	git config coreboot.prepared-commit "$requested"
	printf 'EDK2 %s resolved to %s\n' "$revision" "$requested"
fi
