#!/bin/bash
set -euo pipefail
test "$#" = 6 || { echo 'usage: run-two.sh CB FW EXPECTED_CDK2_HEAD VBOOT PASSED_BUILD NEW_RUN_OUTPUT' >&2; exit 2; }
cb=$(realpath "$1") fw=$(realpath "$2") expected=$3 vboot=$(realpath "$4") built=$(realpath "$5") out=$6
recipe=$(cd -- "$(dirname -- "$0")" && pwd -P)
test -z "${PYTHONOPTIMIZE+x}" || { echo 'PYTHONOPTIMIZE must be absent' >&2; exit 2; }
[[ $expected =~ ^[0-9a-f]{40}$ ]] || { echo 'expected CDK2 HEAD must be full lowercase 40-hex' >&2; exit 2; }
test "$expected" = 6faf053c28d7b33de30daeaeae8408340685d006
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export LANG=C LC_ALL=C TMPDIR=/home/sean PYTHONDONTWRITEBYTECODE=1
test -z "$(env | sed -n '/^CDK2_/p')"
test "$(git -C "$cb" rev-parse HEAD)" = 3e2609c8938a06c6bb782474c7a01209745a4319
test "$(git -C "$fw" rev-parse HEAD)" = "$expected"
test "$(git -C "$vboot" rev-parse HEAD)" = 5c360ef458b0a013d8a6d47724bb0fffb5accbcf
test "$(git -C "$cb" ls-tree HEAD 3rdparty/vboot | awk '{print $3}')" = "$(git -C "$vboot" rev-parse HEAD)"
test -z "$(git -C "$vboot" status --porcelain --untracked-files=normal)"
test "$(cat "$built/aggregate.status")" = 0
python3 "$recipe/receipt.py" verify-files "$built/artifacts.json"
test ! -e "$out"
mkdir -p "$out"
out=$(realpath "$out") rom=$built/tpm-acceptance.rom cbfstool=$cb/build/util/cbfstool/cbfstool
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
sources=("$cb" "$fw" "$vboot" "$fw/3rdparty/bearssl" "$fw/3rdparty/lvgl")
python3 "$recipe/receipt.py" snapshot "${sources[@]}" > "$out/source-tools-before.json"
cmp "$built/source-tools-before.json" "$out/source-tools-before.json"
python3 "$recipe/receipt.py" native "$fw" "$built/payload" "$cb" > "$out/native-inputs-before.json"
cmp "$built/native-inputs-after.json" "$out/native-inputs-before.json"
python3 "$recipe/receipt.py" toolchain "$fw" "$built/toolchain" "$cb" > "$out/selected-tools-before.json"
cmp "$built/selected-tools-after.json" "$out/selected-tools-before.json"
sha256sum "$recipe"/* > "$out/recipe-before.sha256"
nvme=$fw/util/qemu/fixtures/nvme-final-c4bec.raw usb=$fw/util/qemu/fixtures/usb.raw
test "$(sha256sum "$nvme" | cut -d' ' -f1)" = 054a451e67b291adc7390301ef21c9df2de3582e768612cc66459e58d1305e36
test "$(sha256sum "$usb" | cut -d' ' -f1)" = 3b6a07d0d404fab4e23b6d34bc6696a6a312dd92821332385e5af7c01c421351
bound=("$rom" "$rom.manifest.json" "$rom.direct-provenance.tsv" "$cbfstool" "$nvme" "$usb" "$cb/.config" "$built/payload/.config" "$built/payload/native/cdk2-coreboot-image.elf")
python3 "$recipe/receipt.py" files "${bound[@]}" > "$out/inputs-before.json"
aggregate=0
finish() {
    local status=$?
    trap - EXIT
    set +e
    python3 "$recipe/receipt.py" snapshot "${sources[@]}" > "$out/source-tools-after.json"
    test "$?" = 0 || aggregate=1
    cmp "$out/source-tools-before.json" "$out/source-tools-after.json" > "$out/source-tools-check.log" 2>&1 || aggregate=1
    python3 "$recipe/receipt.py" files "${bound[@]}" > "$out/inputs-after.json"
    test "$?" = 0 || aggregate=1
    cmp "$out/inputs-before.json" "$out/inputs-after.json" > "$out/inputs-check.log" 2>&1 || aggregate=1
    python3 "$recipe/receipt.py" native "$fw" "$built/payload" "$cb" > "$out/native-inputs-after.json"
    test "$?" = 0 || aggregate=1
    cmp "$out/native-inputs-before.json" "$out/native-inputs-after.json" > "$out/native-inputs-check.log" 2>&1 || aggregate=1
    python3 "$recipe/receipt.py" toolchain "$fw" "$built/toolchain" "$cb" > "$out/selected-tools-after.json"
    test "$?" = 0 || aggregate=1
    cmp "$out/selected-tools-before.json" "$out/selected-tools-after.json" > "$out/selected-tools-check.log" 2>&1 || aggregate=1
    sha256sum -c "$out/recipe-before.sha256" > "$out/recipe-check.log" 2>&1 || aggregate=1
    test "$status" = 0 || aggregate=1
    printf '%s\n' "$aggregate" > "$out/aggregate.status"
    exit "$aggregate"
}
trap finish EXIT
stage() {
    local label=$1; shift
    python3 "$recipe/receipt.py" argv "$@" > "$out/$label.argv.json"
    local status=0
    /usr/bin/time -p -o "$out/$label.time" "$@" > "$out/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$out/$label.status"
    test "$status" = 0 || aggregate=1
    return "$status"
}
for lane in tpm-linux; do
    run=$out/$lane
    mkdir "$run"
    # Original actual canonical disk, BEFORE launch. This is not an NVMe seed.
    cp --reflink=auto "$nvme" "$run/nvme-before.raw"
    cmp "$nvme" "$run/nvme-before.raw"
    cp --reflink=auto "$usb" "$run/usb-before.raw"
    cmp "$usb" "$run/usb-before.raw"
    tpm=1; test "$lane" != tpm-absent-dma-safe || tpm=0
    # Bash xtrace records the actual expanded source-owned QEMU invocation.
    # Dedicated inherited fd avoids contaminating QEMU stdout/stderr diagnostics.
    exec 9>"$run/qemu-command.txt"
    stage "$lane-run" env -i PATH="$PATH" TMPDIR="$TMPDIR" LANG=C LC_ALL=C PYTHONDONTWRITEBYTECODE=1 \
        BASH_XTRACEFD=9 'PS4=+${BASH_SOURCE}:${LINENO}: ' \
        CDK2_AB_ENABLE_ESP=0 CDK2_AB_ENABLE_NVME=1 CDK2_AB_ENABLE_USB=1 \
        CDK2_AB_ENABLE_SATA=0 CDK2_AB_ENABLE_SD=0 CDK2_AB_ENABLE_NETWORK=0 \
        "CDK2_AB_ENABLE_TPM=$tpm" CDK2_AB_CPU=max CDK2_AB_SMP=1 \
        CDK2_AB_ALLOW_SHUTDOWN=1 CDK2_AB_NVME_IMAGE=nvme-final-c4bec.raw \
        "CDK2_AB_CBFSTOOL=$cbfstool" bash -x "$fw/util/qemu/bin/run-one.sh" "$lane" "$rom" "$run" 180 || true
    exec 9>&-
    if test -f "$run/manifest.json"; then
        # Actual runner's captured wait status, not the outer shell exit status.
        jq -er '.qemu_status | select(type == "number" and . == 0)' "$run/manifest.json" > "$run/qemu.status" || aggregate=1
        test "$(rg -c 'run-deadline.py .* -- qemu-system-x86_64 ' "$run/qemu-command.txt")" = 1 || aggregate=1
        stage "$lane-oracle" python3 "$fw/util/qemu/bin/assert-linear-run.py" "$lane" "$run" --output "$out/$lane-assertions.json" || true
        stage "$lane-media" python3 "$recipe/receipt.py" media "$fw" "$run" "$rom" "$cbfstool" || true
        if test "$lane" = tpm-linux; then
            # Preserve an empty qemu.log if legitimate; current freezer may reject it.
            stage "$lane-freezer" python3 "$fw/util/qemu/bin/freeze-tpm-evidence.py" "$run" "$rom" "$rom.manifest.json" "$cb" "$fw" "$cbfstool" "$out/tpm-evidence.json" || true
        fi
    else
        aggregate=1
    fi
done
