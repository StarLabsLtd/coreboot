#!/bin/bash
set -euo pipefail
export LC_ALL=C
# Root executes only after independent source review. No deletion.
repo=/home/sean/Documents/cdk2
expected=62352715633271454b9c2edc10c055c12fb5deef
dest=${1:?new evidence destination required}
[[ "$dest" == /home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/future-floor-retirement ]]
[[ ! -e "$dest" && ! -L "$dest" ]]
git -C "$repo" verify-commit "$expected"
[[ "$(git -C "$repo" rev-parse "$expected^{commit}")" == "$expected" ]]
roots=(
    /home/sean/cdk2-system-fmp-future-floor.4mzvLV
    /home/sean/cdk2-system-fmp-future-floor.ByrCWa
    /home/sean/cdk2-system-fmp-future-floor.CEC3F3
    /home/sean/cdk2-system-fmp-future-floor.Co4F8l
    /home/sean/cdk2-system-fmp-future-floor.JjtBYh
    /home/sean/cdk2-system-fmp-future-floor.MmM6so
    /home/sean/cdk2-system-fmp-future-floor.NrllFI
    /home/sean/cdk2-system-fmp-future-floor.XQI4NL
    /home/sean/cdk2-system-fmp-future-floor.ZT4HJf
    /home/sean/cdk2-system-fmp-future-floor.cU6MdY
    /home/sean/cdk2-system-fmp-future-floor.dZ0SHC
    /home/sean/cdk2-system-fmp-future-floor.dyrnpU
    /home/sean/cdk2-system-fmp-future-floor.hzPopE
    /home/sean/cdk2-system-fmp-future-floor.jklBY3
    /home/sean/cdk2-system-fmp-future-floor.lBCtxP
    /home/sean/cdk2-system-fmp-future-floor.o1QNmm
    /home/sean/cdk2-system-fmp-future-floor.yZ3lrW
    /home/sean/cdk2-system-fmp-future-floor.yvqKqn
)
# Validate every finite root before creating archive.
for root in "${roots[@]}"; do
    [[ -d "$root" && ! -L "$root" ]]
    [[ -z "$(find "$root" -type l -print -quit)" ]]
    while IFS= read -r -d '' input; do
        rel=${input#"$root"/}
        case "$rel" in
        src/boot/coreboot.h|src/modules/system_fmp/session.c|src/modules/system_fmp/transport.c|src/modules/system_fmp/endpoint-seal.c|src/modules/system_fmp/flags.c|src/modules/system_fmp/handoff-seal.c|src/modules/system_fmp/provider-alias.c|session-mutant.c|transport-mutant.c) ;;
        src/*|*.c|*.h|*.S|*.s|*.cc|*.cpp|*.cxx|*.hpp|*.sh|*.bash|*.py|*.pl|*.awk|*.mk|*.patch|*.diff|Makefile|*/Makefile|Kconfig|*/Kconfig|*.tar|*.tar.gz|*.tgz)
            echo "unexpected source or recipe input: $input" >&2; exit 1 ;;
        *) ;;
        esac
    done < <(find "$root" -type f -print0)
done
mkdir -- "$dest"
mkdir -- "$dest/blobs"
printf 'sha256\toriginal\tstored-or-retained\n' > "$dest/mapping.tsv"
: > "$dest/original.sha256"
: > "$dest/excluded.tsv"
# Exact named owned sources plus flat receipt text only; no certificate/key/media/object copying.
for root in "${roots[@]}"; do
    while IFS= read -r -d '' input; do
        rel=${input#"$root"/}
        selected=0
        case "$rel" in
        src/*|session-mutant.c|transport-mutant.c|*.log|source-before.sha256) selected=1 ;;
        esac
        if (( ! selected )); then
            printf '%s\t%s\n' "$(stat -c %s -- "$input")" "$input" >> "$dest/excluded.tsv"
            continue
        fi
        # Top-level logs only: src inputs were exhaustively validated above.
        [[ "$rel" == src/* || "$rel" != */* ]]
        [[ -f "$input" && ! -L "$input" ]]
        digest=$(sha256sum -- "$input")
        digest=${digest%% *}
        printf '%s  %s\n' "$digest" "$input" >> "$dest/original.sha256"
        retained=
        if [[ "$rel" == src/* ]] && git -C "$repo" cat-file -e "$expected:$rel" 2>/dev/null; then
            git_digest=$(git -C "$repo" show "$expected:$rel" | sha256sum)
            git_digest=${git_digest%% *}
            if [[ "$git_digest" == "$digest" ]]; then
                retained="git:$expected:$rel"
            fi
        fi
        if [[ -n "$retained" ]]; then
            printf '%s\t%s\t%s\n' "$digest" "$input" "$retained" >> "$dest/mapping.tsv"
        else
            stored="blobs/$digest.txt"
            if [[ ! -e "$dest/$stored" ]]; then
                cp -- "$input" "$dest/$stored"
            fi
            cmp -- "$input" "$dest/$stored"
            printf '%s\t%s\t%s\n' "$digest" "$input" "$stored" >> "$dest/mapping.tsv"
        fi
    done < <(find "$root" -type f -print0 | sort -z)
done
sha256sum -c "$dest/original.sha256"
cp -- "/home/sean/future-floor-retirement-collector.ytczfh/README.txt" "$dest/README.txt"
cp -- "/home/sean/future-floor-retirement-collector.ytczfh/collect.sh" "$dest/collector.sh"
(
    cd "$dest"
    find . -type f ! -name archive.sha256 -print0 | sort -z | xargs -0 sha256sum > archive.sha256
    sha256sum -c archive.sha256
)
# This is archival execution only, not historical test replay or cleanup authority.
