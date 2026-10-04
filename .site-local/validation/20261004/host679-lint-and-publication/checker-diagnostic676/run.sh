#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin HOME=/home/sean LANG=C LC_ALL=C
receipt=/home/sean/checkpatch-wire-diagnosis.8X1FxT
source=/home/sean/Documents/.cdk2-worktrees/checkpatch-source-boundaries-after661
input=/home/sean/checkpatch-boundaries-host.mXHLoF/fat.log
sha256sum "$receipt/run.sh" "$receipt/replay.c" "$input" \
    "$source/util/lint/cdk2-checkpatch-filter.c" "$source/src/modules/fat/protocol.c" \
    /usr/bin/gcc > "$receipt/before.sha256"
for mode in o0 o2; do
    status=0
    /usr/bin/gcc -std=c11 -Wall -Wextra -Werror -g "-${mode^^}" \
        "$receipt/replay.c" -o "$receipt/$mode" > "$receipt/$mode-compile.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$receipt/$mode-compile.status"
    test "$status" = 0
    "$receipt/$mode" "$source" "$input" > "$receipt/$mode-replay.log"
done
sha256sum -c "$receipt/before.sha256" > "$receipt/closure.log"
