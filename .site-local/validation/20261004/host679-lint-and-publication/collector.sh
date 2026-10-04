#!/bin/bash
set -euo pipefail
recipe=/home/sean/host679-lint-publication-collector.KXxyN3
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/host679-lint-and-publication
test ! -e "$destination"
status_is()
{
    test "$(cat "$1/$2.status")" = "$3"
}
pack671=/home/sean/tcg-hash-pack32-host.l71pAn
test -d "$pack671"
helper_first_failed672=/home/sean/efivar-unlock-contract-host.phWpE0
test -d "$helper_first_failed672"
helper_retry672=/home/sean/efivar-unlock-contract-host-retry.OKE5zj
test -d "$helper_retry672"
pcr_placement673674=/home/sean/tcg2-boot-variable-pcr1-host-final.20261004-r2
test -d "$pcr_placement673674"
direct_link675=/home/sean/direct-phase-report-link-final.20261004-r3
test -d "$direct_link675"
checker_first_failed676=/home/sean/checkpatch-boundaries-host.mXHLoF
test -d "$checker_first_failed676"
checker_diagnostic676=/home/sean/checkpatch-wire-diagnosis.8X1FxT
test -d "$checker_diagnostic676"
checker_retry676=/home/sean/checkpatch-boundaries-host-retry.otoujN
test -d "$checker_retry676"
helper_public677=/home/sean/efivar-unlock-public-final.20261004-r1
test -d "$helper_public677"
dxe_joined_publication=/home/sean/dxe-publication-joined-final.20261004-r2
test -d "$dxe_joined_publication"
full_lint677_failed=/home/sean/full-lint-after677-final.20261004-r1
test -d "$full_lint677_failed"
for directory in "$pack671" "$helper_retry672" "$pcr_placement673674" "$direct_link675" "$checker_retry676" "$helper_public677" "$dxe_joined_publication"; do
    for name in execution closure aggregate; do status_is "$directory" "$name" 0; done
done
status_is "$pack671" old-shim 1
status_is "$pack671" old-shim-opposition 0
for directory in "$helper_first_failed672" "$helper_retry672"; do
    status_is "$directory" old-contract 134
    status_is "$directory" old-contract-opposition 0
done
for directory in "$helper_first_failed672" "$checker_first_failed676"; do
    status_is "$directory" execution 1
    status_is "$directory" closure 0
    status_is "$directory" aggregate 1
done
status_is "$helper_first_failed672" fixture-checkpatch 1
status_is "$checker_first_failed676" fat 1
status_is "$checker_retry676" fat 0
for mode in o0 o2; do
    status_is "$pcr_placement673674" "$mode-old-driver" 134
    status_is "$pcr_placement673674" "$mode-old-driver-opposition" 0
    status_is "$checker_diagnostic676" "$mode-compile" 0
    grep -Fxq 'filter=1 clean=0' "$checker_diagnostic676/$mode-replay.log"
done
status_is "$pcr_placement673674" original-initializer-compile 1
status_is "$pcr_placement673674" original-initializer-opposition 0
status_is "$pcr_placement673674" old-oracle 1
status_is "$pcr_placement673674" old-oracle-opposition 0
for profile in normal legacy; do
    status_is "$direct_link675" "$profile-old-script" 1
    status_is "$direct_link675" "$profile-old-script-opposition" 0
    status_is "$direct_link675" "$profile-direct-phase" 0
done
for status in "$dxe_joined_publication"/*.status; do test "$(cat "$status")" = 0; done
test "$(find "$dxe_joined_publication" -maxdepth 1 -name '*.status' -type f | wc -l)" = 22
test -f "$dxe_joined_publication/outputs.sha256"
test ! -s "$dxe_joined_publication/outputs.sha256"
# Completion admission deliberately refuses the still-running full lint stage.
for name in execution aggregate; do status_is "$full_lint677_failed" "$name" 1; done
status_is "$full_lint677_failed" closure 0
status_is "$full_lint677_failed" lint 2
status_is "$full_lint677_failed" lint-extended 0
status_is "$full_lint677_failed" full-checkpatch 1
test "$(find "$full_lint677_failed" -maxdepth 1 -name '*.status' -type f | wc -l)" = 14
publication=$dxe_joined_publication/tmp/dxe-capsule-publication.xp1cRo
test -d "$publication"
test "$(grep -Fc 'Actual minimal-variable capsule publication: ordered dispatch/owners/duplicate/transaction PASS' "$dxe_joined_publication/actual-publication.log")" = 2
for cause in dispatch count runtime-start fmp-start duplicate transaction; do
    grep -Fq "Actual publication $cause guard: assertion134/byte-exact inverse/no SAN diagnostic PASS" "$dxe_joined_publication/actual-publication.log"
    grep -Fq 'verify_publication: Assertion' "$publication/$cause.log"
    grep -Fq 'status == expected' "$publication/$cause.log"
    ! grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' "$publication/$cause.log"
done
sha256sum --quiet -c <<'PINS'
eb6cb0aef0e48ecc5913f35bb0eeb5f371430a8e88a4f31b2ed94a4c3623bece  /home/sean/tcg-hash-pack32-host.l71pAn/run.sh
2cd8948401342406c630b78ee9bbdde977d881ca00b49f21b4565aa43fe4bfc8  /home/sean/tcg-hash-pack32-host.l71pAn/README.txt
5823c92b9096f71a0deda5c6a7f5ac24e301c7e347e6bb1ebe4a43dd51fbdcc6  /home/sean/efivar-unlock-contract-host.phWpE0/run.sh
08115e4b08d8527a0e0401f6e8c3c9d123063e1ac565060f9bd11c184e23641d  /home/sean/efivar-unlock-contract-host.phWpE0/README.txt
e97243b8e33a0dffbd19c7b62709e7881694941bd2d717f88afa6a6f9ebb00a2  /home/sean/efivar-unlock-contract-host-retry.OKE5zj/run.sh
001db4baa91b4340f91843e710a2493e0c9ea8022ad0306e40158aadcb0c8694  /home/sean/efivar-unlock-contract-host-retry.OKE5zj/README.txt
5846978beff5f3de71e156cbcd53cbb69f9dcd53a2176636bd0996838226e0fc  /home/sean/checkpatch-boundaries-host.mXHLoF/run.sh
8c6b132dcc521b2d619042985aa0d41e51aa86407ab63575e3d586d7854dbc5a  /home/sean/checkpatch-boundaries-host.mXHLoF/README.txt
60064b6300d2c7bf420f71ad5ced0868250d55212b8ce443e129c75b0dd7b306  /home/sean/checkpatch-wire-diagnosis.8X1FxT/run.sh
6c183f2565ba2359bddd5ba481dde348a565f6ad1f487582f26fd2d550308491  /home/sean/checkpatch-wire-diagnosis.8X1FxT/replay.c
961546f528cd3e5260a654cf3eb56273614be1e2c8871a968dba4e08fe0120d1  /home/sean/checkpatch-boundaries-host-retry.otoujN/run.sh
b20a6747865652caa6e3fd08612a1b193368b67b0bf072e99d09318bc3c4d58d  /home/sean/checkpatch-boundaries-host-retry.otoujN/README.txt
a4f82f3e23fd5a7b9fa9af4b80a919668b91e855ed27a094f3ed3bdcfd29ac32  /home/sean/tcg2-boot-variable-pcr1-host-retry-recipe.6lP09v/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/tcg2-boot-variable-pcr1-host-retry-recipe.6lP09v/hash-inputs.py
400687d422fce3ee418a65977c1c0318bfecd318161234ca269457801005ed23  /home/sean/tcg2-boot-variable-pcr1-host-retry-recipe.6lP09v/README.txt
0fce67ef426d50684ade660470fcd807b55c33855f359f31592b2fe2335738ac  /home/sean/direct-phase-report-link-host-retry3-wrap.SRyzRw/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/direct-phase-report-link-host-retry3-wrap.SRyzRw/hash-inputs.py
bf2edf1afa08b71b4c082635e0cf28aee2560885ece9b73d291f2f430e7beaec  /home/sean/direct-phase-report-link-host-retry3-wrap.SRyzRw/README.txt
97e07e443aff0bd10b69099ecaff04e272abd20c470842f3a24fa56872d2a949  /home/sean/efivar-unlock-public-host.YhcaiC/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/efivar-unlock-public-host.YhcaiC/hash-inputs.py
5b298b10cbdd5b735e908df15de5b6c2d07152330f615fc396db721a501e558a  /home/sean/efivar-unlock-public-host.YhcaiC/README.txt
7f885e6c8b7b3fd24c25e98aae496f9a94e9b90306a37d5155b0557e72dfdd71  /home/sean/dxe-publication-joined-host-recipe.ytR2wD/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/dxe-publication-joined-host-recipe.ytR2wD/hash-inputs.py
0209ce400805b861acc08edf0094e30a72d79c0f87152ca69f7cfe77e80e85f0  /home/sean/dxe-publication-joined-host-recipe.ytR2wD/README.txt
ecbe423736b28f46e75729c46e2fa085b54b8549392e1158659634cc8035ca3e  /home/sean/full-lint-after677-recipe.Pnl5Ax/run.sh
827e515738d08dbccc6d1d7064ee595256d58fe875cf03c5604579ee30bbbebb  /home/sean/full-lint-after677-recipe.Pnl5Ax/hash-inputs.py
2ec58be537e97767d5ae624a6c32e6bf4b026e199a77ece42ff1c516e93caafd  /home/sean/full-lint-after677-recipe.Pnl5Ax/README.txt
PINS
mkdir "$destination"
: > "$destination/ORIGINAL_FILES.sha256"
: > "$destination/FILES_MAP.tsv"
copy_file()
{
    local label=$1 path=$2 target
    test -f "$path" && test ! -L "$path"
    target=$destination/$label/${path##*/}
    test ! -e "$target" && test ! -e "$target.gz"
    mkdir -p "$(dirname "$target")"
    sha256sum "$path" >> "$destination/ORIGINAL_FILES.sha256"
    if test "$(stat -c %s "$path")" -gt 1048576; then
        gzip -n -c "$path" > "$target.gz"
        gzip -dc "$target.gz" | cmp "$path" -
        target=$target.gz
    else
        cp -p "$path" "$target"
        cmp "$path" "$target"
    fi
    printf '%s\t%s\n' "$path" "${target#"$destination/"}" >> "$destination/FILES_MAP.tsv"
}
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file pack671 "$path" ;;
    esac
done < <(find /home/sean/tcg-hash-pack32-host.l71pAn -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file helper-first-failed672 "$path" ;;
    esac
done < <(find /home/sean/efivar-unlock-contract-host.phWpE0 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file helper-retry672 "$path" ;;
    esac
done < <(find /home/sean/efivar-unlock-contract-host-retry.OKE5zj -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file pcr-placement673674 "$path" ;;
    esac
done < <(find /home/sean/tcg2-boot-variable-pcr1-host-final.20261004-r2 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file direct-link675 "$path" ;;
    esac
done < <(find /home/sean/direct-phase-report-link-final.20261004-r3 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file checker-first-failed676 "$path" ;;
    esac
done < <(find /home/sean/checkpatch-boundaries-host.mXHLoF -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file checker-diagnostic676 "$path" ;;
    esac
done < <(find /home/sean/checkpatch-wire-diagnosis.8X1FxT -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file checker-retry676 "$path" ;;
    esac
done < <(find /home/sean/checkpatch-boundaries-host-retry.otoujN -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file helper-public677 "$path" ;;
    esac
done < <(find /home/sean/efivar-unlock-public-final.20261004-r1 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file dxe-joined-publication "$path" ;;
    esac
done < <(find /home/sean/dxe-publication-joined-final.20261004-r2 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file full-lint677-failed "$path" ;;
    esac
done < <(find /home/sean/full-lint-after677-final.20261004-r1 -maxdepth 1 -type f -print0 | sort -z)
copy_file pack671 /home/sean/tcg-hash-pack32-host.l71pAn/run.sh
copy_file helper-first-failed672 /home/sean/efivar-unlock-contract-host.phWpE0/run.sh
copy_file helper-retry672 /home/sean/efivar-unlock-contract-host-retry.OKE5zj/run.sh
copy_file checker-first-failed676 /home/sean/checkpatch-boundaries-host.mXHLoF/run.sh
copy_file checker-diagnostic676 /home/sean/checkpatch-wire-diagnosis.8X1FxT/run.sh
copy_file checker-diagnostic676 /home/sean/checkpatch-wire-diagnosis.8X1FxT/replay.c
copy_file checker-retry676 /home/sean/checkpatch-boundaries-host-retry.otoujN/run.sh
copy_file pcr-recipe /home/sean/tcg2-boot-variable-pcr1-host-retry-recipe.6lP09v/run.sh
copy_file pcr-recipe /home/sean/tcg2-boot-variable-pcr1-host-retry-recipe.6lP09v/hash-inputs.py
copy_file pcr-recipe /home/sean/tcg2-boot-variable-pcr1-host-retry-recipe.6lP09v/README.txt
copy_file direct-recipe /home/sean/direct-phase-report-link-host-retry3-wrap.SRyzRw/run.sh
copy_file direct-recipe /home/sean/direct-phase-report-link-host-retry3-wrap.SRyzRw/hash-inputs.py
copy_file direct-recipe /home/sean/direct-phase-report-link-host-retry3-wrap.SRyzRw/README.txt
copy_file helper-public-recipe /home/sean/efivar-unlock-public-host.YhcaiC/run.sh
copy_file helper-public-recipe /home/sean/efivar-unlock-public-host.YhcaiC/hash-inputs.py
copy_file helper-public-recipe /home/sean/efivar-unlock-public-host.YhcaiC/README.txt
copy_file dxe-joined-recipe /home/sean/dxe-publication-joined-host-recipe.ytR2wD/run.sh
copy_file dxe-joined-recipe /home/sean/dxe-publication-joined-host-recipe.ytR2wD/hash-inputs.py
copy_file dxe-joined-recipe /home/sean/dxe-publication-joined-host-recipe.ytR2wD/README.txt
copy_file full-lint-recipe /home/sean/full-lint-after677-recipe.Pnl5Ax/run.sh
copy_file full-lint-recipe /home/sean/full-lint-after677-recipe.Pnl5Ax/hash-inputs.py
copy_file full-lint-recipe /home/sean/full-lint-after677-recipe.Pnl5Ax/README.txt
for name in dependencies.make dependencies.txt inputs-before.sha256; do
    copy_file publication-artifacts "$publication/$name"
done
for cause in dispatch count runtime-start fmp-start duplicate transaction; do
    for suffix in log c inverse.c; do
        copy_file publication-artifacts "$publication/$cause.$suffix"
    done
done
sha256sum --quiet -c "$destination/ORIGINAL_FILES.sha256"
cp -p "$recipe/collect.sh" "$destination/collector.sh"
cp -p "$recipe/README.md" "$destination/README.md"
cmp "$recipe/collect.sh" "$destination/collector.sh"
cmp "$recipe/README.md" "$destination/README.md"
(
    cd "$destination"
    find . -type f ! -name ARCHIVE.sha256 -print0 | sort -z | xargs -0 sha256sum > ARCHIVE.sha256
    sha256sum --quiet -c ARCHIVE.sha256
)
