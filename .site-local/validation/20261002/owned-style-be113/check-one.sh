#!/bin/bash
set -u
root=/home/sean/Documents/cdk2
proof=/home/sean/cdk2-owned-style-be113.oM6Y1P
path=${1:?source path required}
name=${path//\//__}
cd "$root" || exit 2
exec 3<"$proof/checkpatch-filter"
CDK2_CHECKPATCH_EXEC_FD=3 "$proof/checkpatch-filter" "$root" --show-types --file \
	--quiet --max-line-length 96 "$path" > "$proof/logs/$name.log" 2>&1
status=$?
printf '%s\t%s\n' "$status" "$path" > "$proof/status/$name.tsv"
exit 0
