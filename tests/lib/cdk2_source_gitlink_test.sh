#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
verify=$root/payloads/external/cdk2/verify-source.sh
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
upstream=$temporary/upstream
outer=$temporary/outer
source_path=payloads/external/cdk2/cdk2

git init -q "$upstream"
git -C "$upstream" config user.name 'CDK2 test'
git -C "$upstream" config user.email 'cdk2-test@example.invalid'
cat >"$upstream/Makefile" <<'EOF'
.PHONY: coreboot-stage
coreboot-stage:
	@cp revision.txt "$(OUTPUT)"
EOF
printf '%s\n' old >"$upstream/revision.txt"
git -C "$upstream" add Makefile revision.txt
git -C "$upstream" commit -q -m old
old=$(git -C "$upstream" rev-parse HEAD)
printf '%s\n' staged >"$upstream/revision.txt"
git -C "$upstream" add revision.txt
git -C "$upstream" commit -q -m staged
staged=$(git -C "$upstream" rev-parse HEAD)
git -C "$upstream" checkout -q "$old"

git init -q "$outer"
git -C "$outer" config user.name 'CDK2 test'
git -C "$outer" config user.email 'cdk2-test@example.invalid'
git -C "$outer" -c protocol.file.allow=always submodule add -q \
	"$upstream" "$source_path"
git -C "$outer/$source_path" checkout -q "$old"
git -C "$outer" add "$source_path"
git -C "$outer" commit -q -m base

# HEAD retains the old gitlink while the index stages the future pin.
git -C "$outer/$source_path" checkout -q "$staged"
git -C "$outer" add "$source_path"
test "$(git -C "$outer" rev-parse "HEAD:$source_path")" = "$old"
test "$(git -C "$outer" rev-parse ":$source_path")" = "$staged"
git -C "$outer" -c protocol.file.allow=always submodule update -q \
	--init --checkout --recursive -- "$source_path"
(cd "$outer" && "$verify" "$source_path")
make -s -C "$outer/$source_path" coreboot-stage \
	OUTPUT="$temporary/built-revision.txt"
grep -qx staged "$temporary/built-revision.txt"

printf '%s\n' dirty >>"$outer/$source_path/revision.txt"
if (cd "$outer" && "$verify" "$source_path") >/dev/null 2>&1; then
	echo 'dirty staged source was accepted' >&2
	exit 1
fi
git -C "$outer/$source_path" checkout -q -- revision.txt
printf '%s\n' untracked >"$outer/$source_path/untracked"
if (cd "$outer" && "$verify" "$source_path") >/dev/null 2>&1; then
	echo 'untracked staged source was accepted' >&2
	exit 1
fi

printf '%s\n' 'CDK2 staged gitlink source authority: PASS'
