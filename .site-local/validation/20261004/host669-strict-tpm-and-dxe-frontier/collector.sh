#!/bin/bash
set -euo pipefail
recipe=/home/sean/host669-strict-tpm-dxe-collector.yem2Aq
destination=/home/sean/Documents/.coreboot-worktrees/validation-evidence-20261001/.site-local/validation/20261004/host669-strict-tpm-and-dxe-frontier
test ! -e "$destination"
legacy_boot667=/home/sean/tcg2-legacy-boot-digest-host-gates.r087g9
pcr1_placement_failed=/home/sean/tcg2-boot-variable-pcr1-host-final.20261004-r1
os_fixture668=/home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ
private_names669=/home/sean/private-diagnostic-names-host.PP4WaV
wrong_vboot_root_prelaunch=/home/sean/tpm-current-os-guest-after661
strict_tpm_present=/home/sean/tpm-current-os-guest-correct-vboot-after661
dxe_first_failed=/home/sean/dxe-publication-final.20261004-r1
direct_link_first_failed=/home/sean/direct-phase-report-link-final.20261004-r1
direct_link_second_failed=/home/sean/direct-phase-report-link-final.20261004-r2
linear_oracle670=/home/sean/linear-capsule-oracle-final.20261004-r1
for directory in "$legacy_boot667" "$os_fixture668" "$private_names669"; do
    for name in execution closure aggregate; do test "$(cat "$directory/$name.status")" = 0; done
done
for name in execution closure aggregate actual-full-script public-linear-order; do
    test "$(cat "$linear_oracle670/$name.status")" = 0
done
test "$(cat "$linear_oracle670/old-script.status")" = 1
test "$(cat "$linear_oracle670/old-script-opposition.status")" = 0
for mode in o0 o2; do
    test "$(cat "$legacy_boot667/$mode-old-source.status")" = 1
    test "$(cat "$legacy_boot667/$mode-old-source-opposition.status")" = 0
done
for directory in "$pcr1_placement_failed" "$dxe_first_failed" "$direct_link_first_failed" "$direct_link_second_failed"; do
    test "$(cat "$directory/aggregate.status")" = 1
    test "$(cat "$directory/execution.status")" = 1
    test "$(cat "$directory/closure.status")" = 0
done
test "$(cat "$pcr1_placement_failed/public-entry.status")" = 2
test "$(cat "$pcr1_placement_failed/o0-driver-compile.status")" = 1
test "$(cat "$wrong_vboot_root_prelaunch/aggregate.status")" = 1
test ! -e "$wrong_vboot_root_prelaunch/tpm-linux"
test ! -e "$wrong_vboot_root_prelaunch/tpm-linux-run.status"
test "$(cat "$dxe_first_failed/actual-publication.status")" = 1
test "$(cat "$dxe_first_failed/native-dxe-core-test.status")" = 2
for directory in "$direct_link_first_failed" "$direct_link_second_failed"; do
    for profile in normal legacy; do
        test "$(cat "$directory/$profile-old-script.status")" = 1
        test "$(cat "$directory/$profile-old-script-opposition.status")" = 0
    done
done
test "$(cat "$direct_link_first_failed/normal-direct-phase.status")" = 2
test "$(cat "$direct_link_first_failed/legacy-direct-phase.status")" = 2
test "$(cat "$direct_link_second_failed/normal-direct-phase.status")" = 2
test "$(cat "$direct_link_second_failed/legacy-direct-phase.status")" = 0
for status in "$strict_tpm_present"/*.status; do test "$(cat "$status")" = 0; done
jq -e '.name == "tpm-linux" and .qemu_status == 0 and .devices.tpm == 1' "$strict_tpm_present/tpm-linux/manifest.json" >/dev/null
test "$(cat "$strict_tpm_present/tpm-linux/qemu.status")" = 0
for marker in TPM_EVENT_LOG_HEX_BEGIN TPM_EVENT_LOG_HEX_END TPM_PCR45_BANKS_BEGIN TPM_PCR45_BANKS_END CDK2_RUNTIME_OK 'reboot: Power down'; do
    grep -Fq "$marker" "$strict_tpm_present/tpm-linux/serial.log"
done
for prefix in source-tools inputs native-inputs selected-tools; do
    cmp "$strict_tpm_present/$prefix-before.json" "$strict_tpm_present/$prefix-after.json"
done
sha256sum --quiet -c <<'PINS'
9105a7f3669f32279ff22f406f4d96a997d7814ddf0085408a4cec2a160b9470  /home/sean/linear-capsule-oracle-host-recipe.vr8y1R/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/linear-capsule-oracle-host-recipe.vr8y1R/hash-inputs.py
c737f5817d8aae94341583650b6f7d4b6437688964bcf3ed6efc4580816b1e43  /home/sean/linear-capsule-oracle-host-recipe.vr8y1R/README.txt
04060bb94ec7bfa8c573ab24139833a821c756f2cdd914cb8f1997b739ad9744  /home/sean/tcg2-legacy-boot-digest-host-gates.r087g9/run.sh
9119c42914f2d1bcc33624e4805c7210195cd7e92c695382bdd3d2226491e8c9  /home/sean/tcg2-legacy-boot-digest-host-gates.r087g9/README.txt
85b761b3d65d168e796cbe1ba63b191cd0ea83cd9c26760c2f5a14757ed4393c  /home/sean/tcg2-boot-variable-pcr1-host-recipe.5JMXli/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/tcg2-boot-variable-pcr1-host-recipe.5JMXli/hash-inputs.py
efafe9ea3f9b75eafd408f9b3048b01570a00ef87908d93a269c5aca8d50685d  /home/sean/tcg2-boot-variable-pcr1-host-recipe.5JMXli/README.txt
4b033b7e8028811e80d53c9c7d865a00845ec776099f2d0858541b0267424812  /home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ/run.sh
0ec7b71150b7ac352e7a28aabe70a5d73e1c23371a133060defa59feea20f981  /home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ/actual-compiler-inputs.mk
0a3f5351e110bfd8286e3d3adb8b6304114bae9b459c143236d2892b5346ac52  /home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ/README.txt
7b13528695ae202ede137498407e48d171453c6d8b1eb1d5d4ddb793430c8923  /home/sean/private-diagnostic-names-host.PP4WaV/run.sh
431656a04d7a5844f373ef2a946778f3c56c42cb2e5b30e25302807e5c134b6f  /home/sean/private-diagnostic-names-host.PP4WaV/verify-renames.py
939d05286d564a19782c5a7de960cd08b766800183e63a4e6417f3f0bb1737ef  /home/sean/private-diagnostic-names-host.PP4WaV/README.txt
da56533ceff4e8a73fa17764671fede8752bcaec4ddb1a08f61ffaad37d95966  /home/sean/tpm-current-os-guest-recipes.jNWZGi/run-present.sh
35e3eb7d9b7d82b60da90c0548b3196fbb3f42e75249e7fe87c58843b0c4eaa7  /home/sean/tpm-current-os-guest-recipes.jNWZGi/receipt.py
5deab2f54ba1c67f443cafba3ee26657f21ad8265483337230e66876aeed9126  /home/sean/tpm-current-os-guest-recipes.jNWZGi/README.txt
c39f7c4ac3493315b99ed52682410eb10b335c77bb146404532359c0109c86bf  /home/sean/dxe-publication-host-recipe.qMDV5R/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/dxe-publication-host-recipe.qMDV5R/hash-inputs.py
02e7e216371d4d66c9e91f943210b0a1b9f63cfd29800a02d51a360f1109a28a  /home/sean/dxe-publication-host-recipe.qMDV5R/README.txt
8737f4433899bc9a3e0c39f7c0b028c87769b127e2b31fcaed2af252cb3b8684  /home/sean/direct-phase-report-link-host-recipe.QdmqHo/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/direct-phase-report-link-host-recipe.QdmqHo/hash-inputs.py
7c30ddd395a09f476695a3f4c0a62f4c37818c862d009969466ba0a2d346d38b  /home/sean/direct-phase-report-link-host-recipe.QdmqHo/README.txt
a41b3e1448d629e2687313e4e401a45a6253301b0637003f185a9345b338bce5  /home/sean/direct-phase-report-link-host-retry.su7aoY/run.sh
76700d9a6ccc905aafdee2ac0f8704908b64ed5f91007da31d3d22ea690a37fe  /home/sean/direct-phase-report-link-host-retry.su7aoY/hash-inputs.py
dc31a0714f3490be595ddf86b7ca65a23ed7463b8573a341bfb554ffda3a4eaa  /home/sean/direct-phase-report-link-host-retry.su7aoY/README.txt
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
        copy_file legacy-boot667 "$path" ;;
    esac
done < <(find /home/sean/tcg2-legacy-boot-digest-host-gates.r087g9 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file pcr1-placement-failed "$path" ;;
    esac
done < <(find /home/sean/tcg2-boot-variable-pcr1-host-final.20261004-r1 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file os-fixture668 "$path" ;;
    esac
done < <(find /home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file private-names669 "$path" ;;
    esac
done < <(find /home/sean/private-diagnostic-names-host.PP4WaV -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file wrong-vboot-root-prelaunch "$path" ;;
    esac
done < <(find /home/sean/tpm-current-os-guest-after661 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file strict-tpm-present "$path" ;;
    esac
done < <(find /home/sean/tpm-current-os-guest-correct-vboot-after661 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file dxe-first-failed "$path" ;;
    esac
done < <(find /home/sean/dxe-publication-final.20261004-r1 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file direct-link-first-failed "$path" ;;
    esac
done < <(find /home/sean/direct-phase-report-link-final.20261004-r1 -maxdepth 1 -type f -print0 | sort -z)
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file direct-link-second-failed "$path" ;;
    esac
done < <(find /home/sean/direct-phase-report-link-final.20261004-r2 -maxdepth 1 -type f -print0 | sort -z)
copy_file legacy-boot667 /home/sean/tcg2-legacy-boot-digest-host-gates.r087g9/run.sh
while IFS= read -r -d '' path; do
    case "$path" in *.log|*.json|*.txt|*.time|*.status|*.sha256|*.config|*.tsv|*.command|*.diff|*.list)
        copy_file linear-oracle670 "$path" ;;
    esac
done < <(find /home/sean/linear-capsule-oracle-final.20261004-r1 -maxdepth 1 -type f -print0 | sort -z)
copy_file linear-recipe /home/sean/linear-capsule-oracle-host-recipe.vr8y1R/run.sh
copy_file linear-recipe /home/sean/linear-capsule-oracle-host-recipe.vr8y1R/hash-inputs.py
copy_file linear-recipe /home/sean/linear-capsule-oracle-host-recipe.vr8y1R/README.txt
copy_file pcr1-recipe /home/sean/tcg2-boot-variable-pcr1-host-recipe.5JMXli/run.sh
copy_file pcr1-recipe /home/sean/tcg2-boot-variable-pcr1-host-recipe.5JMXli/hash-inputs.py
copy_file pcr1-recipe /home/sean/tcg2-boot-variable-pcr1-host-recipe.5JMXli/README.txt
copy_file os-fixture668 /home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ/run.sh
copy_file os-fixture668 /home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ/actual-compiler-inputs.mk
copy_file private-names669 /home/sean/private-diagnostic-names-host.PP4WaV/run.sh
copy_file private-names669 /home/sean/private-diagnostic-names-host.PP4WaV/verify-renames.py
copy_file strict-guest-recipe /home/sean/tpm-current-os-guest-recipes.jNWZGi/run-present.sh
copy_file strict-guest-recipe /home/sean/tpm-current-os-guest-recipes.jNWZGi/receipt.py
copy_file strict-guest-recipe /home/sean/tpm-current-os-guest-recipes.jNWZGi/README.txt
copy_file dxe-recipe /home/sean/dxe-publication-host-recipe.qMDV5R/run.sh
copy_file dxe-recipe /home/sean/dxe-publication-host-recipe.qMDV5R/hash-inputs.py
copy_file dxe-recipe /home/sean/dxe-publication-host-recipe.qMDV5R/README.txt
copy_file direct-first-recipe /home/sean/direct-phase-report-link-host-recipe.QdmqHo/run.sh
copy_file direct-first-recipe /home/sean/direct-phase-report-link-host-recipe.QdmqHo/hash-inputs.py
copy_file direct-first-recipe /home/sean/direct-phase-report-link-host-recipe.QdmqHo/README.txt
copy_file direct-second-recipe /home/sean/direct-phase-report-link-host-retry.su7aoY/run.sh
copy_file direct-second-recipe /home/sean/direct-phase-report-link-host-retry.su7aoY/hash-inputs.py
copy_file direct-second-recipe /home/sean/direct-phase-report-link-host-retry.su7aoY/README.txt
for name in manifest.json serial.log debugcon.log qemu.log qemu.status qemu-command.txt swtpm.log cdk2-config.txt coreboot-config.txt; do
    copy_file strict-tpm-present-diagnostics "$strict_tpm_present/tpm-linux/$name"
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
