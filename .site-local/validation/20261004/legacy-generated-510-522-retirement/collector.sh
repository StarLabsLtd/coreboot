#!/bin/bash
set -euo pipefail
export LC_ALL=C
packet=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/legacy-generated-510-522-retirement
retained=/home/sean/Documents/cdk2-validation/retained-inputs/legacy-510-522-combined
vendor=/home/sean/Documents/cdk2/3rdparty/bearssl
vendor_head=8ef7680081c61b486622f2d983c0d3d21e83caad
roots=(
 /home/sean/cdk2-root-production-after510
 /home/sean/cdk2-root-cold-main-after522
 /home/sean/cdk2-root-combined-native.6l4qUG
)
runs=(
 "${roots[0]}/native/protected-variable-fullgraph-production/run.EJ8TkU"
 "${roots[1]}/run.On0z3Y/baseline/run.rA2EY0"
 "${roots[2]}/run.4u6gUG"
)
pins=(
 1ac4561bb895d2452414894cd5cd0716fedfc707d9f3f402d5d90a7806b522f3
 b0a241d9034529a0f951c5543118638db6aeb49b376ab7d66cc004bce5ba9156
 5784b019f17ca29a34675d3218a2fb63e959de01c80d0a4d815b23667bbb9e5c
)
[[ ! -e "$packet" && ! -L "$packet" && ! -e "$retained" && ! -L "$retained" ]]
[[ "$(git -C "$vendor" rev-parse --show-toplevel)" == "$vendor" ]]
[[ "$(git -C "$vendor" rev-parse HEAD)" == "$vendor_head" ]]
[[ -z "$(git -C "$vendor" status --porcelain --untracked-files=all --ignored=matching)" ]]
for i in 0 1 2; do
 [[ -d "${roots[i]}" && ! -L "${roots[i]}" ]]
 [[ -z "$(find "${roots[i]}" -type l -print -quit)" ]]
 printf '%s  %s\n' "${pins[i]}" "${runs[i]}/source-inputs.tar" | sha256sum -c -
done
# No deletion. Root executes only after opposing source review.
printf '%s  %s\n' d087e6697ee09b9c764c43a4b9b2b55fdaa633b693dc318c58b6d4a3c069c718 "${runs[2]}/source/src/modules/dxe_core/entry.c" | sha256sum -c -
mkdir -p -- /home/sean/Documents/cdk2-validation/retained-inputs
mkdir -- "$retained"
mkdir -- "$packet"
mkdir -- "$packet/blobs"
: > "$packet/original.sha256"
printf 'sha256\toriginal\tstored\n' > "$packet/mapping.tsv"
: > "$packet/excluded.tsv"
for i in 0 1 2; do
 run=${runs[i]}
 cp -- "$run/source-inputs.tar" "$retained/source-$i.tar"
 cmp -- "$run/source-inputs.tar" "$retained/source-$i.tar"
 printf '%s  %s\n' "${pins[i]}" "$retained/source-$i.tar" >> "$packet/retained-inputs.sha256"
 # Compare actual stored tar entries; one explicitly preserved test-only mutant differs.
 if [[ "$i" == 2 ]]; then
  tar --compare --exclude=src/modules/dxe_core/entry.c --file="$retained/source-$i.tar" --directory="$run/source" > "$packet/tar-compare-$i.log" 2>&1
 else
  tar --compare --file="$retained/source-$i.tar" --directory="$run/source" > "$packet/tar-compare-$i.log" 2>&1
 fi
 tar -tf "$retained/source-$i.tar" | sed 's#/$##' | sort -u > "$packet/tar-members-$i.txt"
 find "$run/source" -type f -printf '%P\n' | sort -u > "$packet/source-files-$i.txt"
 comm -23 "$packet/source-files-$i.txt" "$packet/tar-members-$i.txt" > "$packet/source-extra-$i.txt"
 # Copied vendor Git pointers dangle: prove every retained content path and byte.
 diff -qr --exclude=.git "$run/source/3rdparty/bearssl" "$vendor" > "$packet/vendor-compare-$i.log"
 (cd "$run/source/3rdparty/bearssl"; find . -type f ! -name .git -print0 | sort -z | xargs -0 sha256sum) > "$packet/vendor-source-$i.sha256"
 (cd "$vendor"; find . -type f ! -name .git -print0 | sort -z | xargs -0 sha256sum) > "$packet/vendor-retained-$i.sha256"
 cmp "$packet/vendor-source-$i.sha256" "$packet/vendor-retained-$i.sha256"
 grep -v '^3rdparty/bearssl/' "$packet/source-extra-$i.txt" > "$packet/source-unretained-$i.txt" || [[ $? == 1 ]]
 [[ ! -s "$packet/source-unretained-$i.txt" ]]
done
printf '%s %s\n' "$vendor_head" "$vendor" > "$packet/retained-vendor.txt"
sha256sum -c "$packet/retained-inputs.sha256"
for root in "${roots[@]}"; do
 while IFS= read -r -d '' input; do
  covered=0
  retention=retained-tar
  for run in "${runs[@]}"; do
   [[ "$input" != "$run/source/"* ]] || covered=1
   [[ "$input" != "$run/source/3rdparty/bearssl/"* ]] || retention=retained-vendor
  done
  # Preserve the one explicit test-only source divergence outside the retained tar.
  [[ "$input" != "${runs[2]}/source/src/modules/dxe_core/entry.c" ]] || covered=0
  for run in "${runs[@]}"; do
   [[ "$input" != "$run/source/3rdparty/bearssl/.git" ]] || covered=0
  done
  if (( covered )); then
   printf '%s\t%s\n' "$retention" "$input" >> "$packet/excluded.tsv"
   continue
  fi
  case "$input" in
  */source-inputs.tar) printf 'retained-tar-file\t%s\n' "$input" >> "$packet/excluded.tsv"; continue ;;
  *.c|*.h|*.S|*.s|*.sh|*.py|*.pl|*.awk|*.mk|*.ld|*.lds|*/Makefile|*/Kconfig|*.patch|*.diff|*.log|*.sha256|*.json|*.jsonl|*.status|*.time|*.txt|*.tsv|*.csv|*.conf|*.command|*.argv|*.cmd|*/command-inputs|*/.config|*/auto.conf|*/config*|*/source/3rdparty/bearssl/.git)
   [[ -f "$input" && ! -L "$input" ]]
   # Avoid mislabeled binary data or private keys in selected textual inputs.
   if [[ -s "$input" ]]; then
    mime=$(file -b --mime-type -- "$input")
    case "$mime" in text/*|application/json|application/x-ndjson|application/x-shellscript) ;;
     *) echo "nontext selected input: $input ($mime)" >&2; exit 1 ;;
    esac
    if rg -q -- '-----BEGIN ([A-Z ]*PRIVATE KEY|OPENSSH PRIVATE KEY)-----' "$input"; then
     echo "private key selected: $input" >&2; exit 1
    fi
   fi
   digest=$(sha256sum -- "$input"); digest=${digest%% *}
   stored="blobs/$digest.txt"
   [[ -e "$packet/$stored" ]] || cp -- "$input" "$packet/$stored"
   cmp -- "$input" "$packet/$stored"
   printf '%s  %s\n' "$digest" "$input" >> "$packet/original.sha256"
   printf '%s\t%s\t%s\n' "$digest" "$input" "$stored" >> "$packet/mapping.tsv"
   ;;
  *.cc|*.cpp|*.cxx|*.hpp|*.bash|*.tar|*.tar.gz|*.tgz)
   echo "unexpected source/recipe/archive: $input" >&2; exit 1 ;;
  *) printf 'generated-unarchived\t%s\t%s\n' "$(stat -c %s -- "$input")" "$input" >> "$packet/excluded.tsv" ;;
  esac
 done < <(find "$root" -type f -print0 | sort -z)
done
sha256sum -c "$packet/original.sha256"
[[ "$(git -C "$vendor" rev-parse HEAD)" == "$vendor_head" ]]
[[ -z "$(git -C "$vendor" status --porcelain --untracked-files=all --ignored=matching)" ]]
cp -- "/home/sean/legacy-generated-retirement-collector.YKwsAo/collect.sh" "$packet/collector.sh"
cp -- "/home/sean/legacy-generated-retirement-collector.YKwsAo/README.txt" "$packet/README.txt"
(cd "$packet"; find . -type f ! -name archive.sha256 -print0 | sort -z | xargs -0 sha256sum > archive.sha256; sha256sum -c archive.sha256)
