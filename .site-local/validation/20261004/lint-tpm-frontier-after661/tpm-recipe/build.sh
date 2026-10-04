#!/bin/bash
set -euo pipefail
# Run ONLY after source review/release. Root supplies two clean source roots.
test "$#" = 5 || { echo 'usage: build.sh CB FW EXPECTED_CDK2_HEAD VBOOT NEW_OUTPUT' >&2; exit 2; }
cb=$(realpath "$1") fw=$(realpath "$2") expected=$3 vboot=$(realpath "$4") out=$5
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
test -z "${PYTHONOPTIMIZE+x}" || { echo 'PYTHONOPTIMIZE must be absent' >&2; exit 2; }
[[ $expected =~ ^[0-9a-f]{40}$ ]] || { echo 'expected CDK2 HEAD must be full lowercase 40-hex' >&2; exit 2; }
test "$expected" != NEXT_SIGNED_READY_HEAD
test "$(git -C "$cb" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test "$(git -C "$fw" rev-parse HEAD)" = "$expected"
test "$(git -C "$vboot" rev-parse HEAD)" = 5c360ef458b0a013d8a6d47724bb0fffb5accbcf
test "$(git -C "$cb" ls-tree HEAD 3rdparty/vboot | awk '{print $3}')" = "$(git -C "$vboot" rev-parse HEAD)"
test -z "$(git -C "$vboot" status --porcelain --untracked-files=normal)"
test ! -e "$cb/.config" && test ! -e "$cb/build"
test ! -e "$out"
mkdir -p "$out"
out=$(realpath "$out") build=$out/payload
mkdir "$build"
printf '1\n' > "$out/aggregate.status"
for name in producer cdk2; do
    case $name in producer) tree=$cb ;; cdk2) tree=$fw ;; esac
    test -z "$(git -C "$tree" status --porcelain --untracked-files=normal)"
    status=0
    git -C "$tree" verify-commit HEAD > "$out/$name-signature.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$out/$name-signature.status"
    test "$status" = 0
done
git -C "$vboot" cat-file commit HEAD > "$out/vboot-upstream-object.txt"
printf '%s\n' 'hash-pinned upstream object; no vendor signature claimed' > "$out/vboot-provenance.txt"
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export LANG=C LC_ALL=C TMPDIR=/home/sean PYTHONDONTWRITEBYTECODE=1
# Caller MUST enter via env -i (documented); refuse inherited behavior controls.
test -z "$(env | sed -n '/^CDK2_/p')"
sources=("$cb" "$fw" "$vboot" "$fw/3rdparty/bearssl" "$fw/3rdparty/lvgl")
for vendor in bearssl lvgl; do
    test "$(git -C "$fw/3rdparty/$vendor" rev-parse HEAD)" = "$(git -C "$fw" ls-tree HEAD "3rdparty/$vendor" | awk '{print $3}')"
done
python3 "$recipe/receipt.py" snapshot "${sources[@]}" > "$out/source-tools-before.json"
sha256sum "$recipe"/* > "$out/recipe-before.sha256"
finish() {
    local status=$?
    trap - EXIT
    set +e
    python3 "$recipe/receipt.py" snapshot "${sources[@]}" > "$out/source-tools-after.json"
    local closure=$?
    cmp "$out/source-tools-before.json" "$out/source-tools-after.json" > "$out/source-tools-check.log" 2>&1
    local comparison=$?
    sha256sum -c "$out/recipe-before.sha256" > "$out/recipe-check.log" 2>&1
    local recipe_status=$?
    test "$closure" = 0 && test "$comparison" = 0 && test "$recipe_status" = 0 || status=1
    printf '%s\n' "$status" > "$out/aggregate.status"
    exit "$status"
}
trap finish EXIT
stage() {
    local label=$1; shift
    python3 "$recipe/receipt.py" argv "$@" > "$out/$label.argv.json"
    local status=0
    /usr/bin/time -p -o "$out/$label.time" "$@" > "$out/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$out/$label.status"
    return "$status"
}
cp "$fw/util/qemu/config/q35-dma-real-requesters.defconfig" "$cb/.config"
cp "$cb/.config" "$out/producer-input.config"
stage late-fifo "$cb/util/scripts/config" --file "$cb/.config" --enable Q35_TPM2_FIFO_PRE_OS_LIFECYCLE
vboot_relative=$(realpath --relative-to="$cb" "$vboot")
test "$(realpath "$cb/$vboot_relative")" = "$vboot"
producer=(make -C "$cb" -j2 UPDATED_SUBMODULES=1 "VBOOT_SOURCE=$vboot_relative" HOSTCC=gcc HOSTCXX=g++)
stage producer-config "${producer[@]}" olddefconfig
for symbol in PAYLOAD_NONE Q35_VTD_DMA_TEST_BACKEND PAYLOAD_DMA_HANDOFF TPM2 TPM_INIT_RAMSTAGE TPM_MEASURED_BOOT Q35_TPM2_FIFO_PRE_OS_LIFECYCLE; do
    grep -qx "CONFIG_${symbol}=y" "$cb/.config"
done
! grep -qx 'CONFIG_PAYLOAD_MM_CMS_CORE=y' "$cb/.config"
cp "$cb/.config" "$out/producer-resolved.config"
stage producer-toolchain "${producer[@]}" build/xcompile
cp "$cb/build/xcompile" "$out/producer-xcompile.txt"
# Existing source-owned selected-tool closure expects this finite receipt layout.
mkdir -p "$out/toolchain/initial9/build"
cp "$cb/.config" "$out/toolchain/initial9/full.config"
cp "$cb/build/xcompile" "$out/toolchain/initial9/build/xcompile"
cmp "$cb/.config" "$out/toolchain/initial9/full.config"
cmp "$cb/build/xcompile" "$out/toolchain/initial9/build/xcompile"
python3 "$recipe/receipt.py" toolchain "$fw" "$out/toolchain" "$cb" > "$out/selected-tools-before.json"
stage producer-build "${producer[@]}" all
cmp "$out/producer-resolved.config" "$cb/.config"
cmp "$out/producer-xcompile.txt" "$cb/build/xcompile"
python3 "$recipe/receipt.py" toolchain "$fw" "$out/toolchain" "$cb" > "$out/selected-tools-after.json"
cmp "$out/selected-tools-before.json" "$out/selected-tools-after.json"
cp "$fw/util/qemu/config/cdk2-q35-acceptance.defconfig" "$build/.config"
cp "$build/.config" "$out/payload-input.config"
payload=(make -C "$fw" -j2 "CDK2_BUILD_DIR=$build" "CDK2_CONFIG=$build/.config" "COREBOOT_TREE=$cb" "COREBOOT_CONFIG=$cb/.config" CC=cc HOSTCC=cc)
stage payload-config "${payload[@]}" olddefconfig
for symbol in STRICT_DIRECT_RUNTIME NATIVE_TCG2 NATIVE_TCG2_REPLACEMENT NATIVE_SMMSTORE_FVB NATIVE_FTW LINEAR_BOOT; do
    grep -qx "CONFIG_CDK2_${symbol}=y" "$build/.config"
done
! grep -qx 'CONFIG_CDK2_PROTECTED_VARIABLE_RUNTIME=y' "$build/.config"
cp "$build/.config" "$out/payload-resolved.config"
stage payload-build "${payload[@]}" native-coreboot-image
cmp "$out/payload-resolved.config" "$build/.config"
python3 "$recipe/receipt.py" native "$fw" "$build" "$cb" > "$out/native-inputs-before.json"
stage pair "$fw/util/qemu/bin/build-cdk2-pair.sh" "$cb" "$fw" "$build/native/cdk2-coreboot-image.elf" "$out/tpm-acceptance.rom" "$cb/.config"
python3 "$recipe/receipt.py" native "$fw" "$build" "$cb" > "$out/native-inputs-after.json"
cmp "$out/native-inputs-before.json" "$out/native-inputs-after.json"
python3 "$recipe/receipt.py" files "$cb/.config" "$cb/build/xcompile" "$cb/build/coreboot.rom" "$cb/build/util/cbfstool/cbfstool" "$build/.config" "$build/include/cdk2/config.h" "$build/native/cdk2-coreboot-image.elf" "$out/tpm-acceptance.rom" "$out/tpm-acceptance.rom.manifest.json" "$out/tpm-acceptance.rom.direct-provenance.tsv" > "$out/artifacts.json"
