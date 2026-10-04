#!/bin/bash
set -euo pipefail
export HOME=/home/sean PATH=/usr/local/bin:/usr/bin:/bin LANG=C LC_ALL=C
export PYTHONDONTWRITEBYTECODE=1
test -z "${PYTHONOPTIMIZE+x}"
ulimit -c 0
source=/home/sean/Documents/.cdk2-worktrees/efivar-unlock-error-contract-after661
receipt=/home/sean/efivar-unlock-contract-host.phWpE0
cd "$source"
mkdir "$receipt/build" "$receipt/tmp" "$receipt/old-source"
export TMPDIR=$receipt/tmp
printf '1\n' > "$receipt/aggregate.status"
test "$(git rev-parse HEAD)" = 6faf053c28d7b33de30daeaeae8408340685d006
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git diff --binary > "$receipt/source-before.diff"
test "$(sha256sum "$receipt/source-before.diff" | cut -d' ' -f1)" = 2120b3eb97968f0a76f2850bc4421ac186cbc762e4f9002c56e0cbc6ac758e5e
git status --porcelain --untracked-files=all > "$receipt/status-before.txt"
git show HEAD:util/qemu/fixtures/linux-mini/efivar-unlock.c > "$receipt/old-source/efivar-unlock.c"
tools=(bash sh git ssh-keygen gcc perl sha256sum cmp cut cat mkdir readlink sed sort xargs env grep awk)
paths()
{
    local tool selected
    for tool in "${tools[@]}"; do
        selected=$(command -v "$tool")
        printf '%s\t%s\t%s\n' "$tool" "$selected" "$(readlink -f "$selected")"
    done
}
paths > "$receipt/tools-before.tsv"
for tool in "${tools[@]}"; do
    selected=$(command -v "$tool")
    sha256sum "$selected" >> "$receipt/tools-before.sha256"
done
for support in cc1 as ld collect2 lto-wrapper; do
    selected=$(gcc "-print-prog-name=$support")
    test -f "$selected" || selected=$(command -v "$selected")
    sha256sum "$selected" >> "$receipt/tools-before.sha256"
done
for support in libgcc.a liblto_plugin.so libasan.so libubsan.so; do
    sha256sum "$(gcc "-print-file-name=$support")" >> "$receipt/tools-before.sha256"
done
sha256sum "$receipt/run.sh" "$receipt/README.txt" "$receipt/old-source/efivar-unlock.c" >> "$receipt/tools-before.sha256"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' | \
    xargs -d '\n' sha256sum > "$receipt/source-before.sha256"
sha256sum "$(perl -MErrno -e 'print $INC{"Errno.pm"}')" >> "$receipt/tools-before.sha256"
config_state()
{
    local path
    for path in "$source/.checkpatch.conf" /home/sean/.checkpatch.conf "$source/.scripts/.checkpatch.conf"; do
        if test -e "$path" || test -L "$path"; then
            test -f "$path" && test ! -L "$path"
            sha256sum "$path"
        else
            printf 'absent %s\n' "$path"
        fi
    done
}
config_state > "$receipt/checker-config-before.txt"
perl -MErrno=ENOSYS -e 'print ENOSYS, "\n"' > "$receipt/unsupported-syscall.txt"
unsupported_syscall=$(cat "$receipt/unsupported-syscall.txt")
test "$unsupported_syscall" -gt 0
gcc -std=c11 -D_GNU_SOURCE -M tests/efivar_unlock_test.c > "$receipt/compiler-inputs.d"
perl -MText::ParseWords=shellwords -0777 -ne 's/\\\n/ /g; for (split /\n/) {s/^[^:]+: // or next; print join("\n",shellwords($_)),"\n";}' "$receipt/compiler-inputs.d" | sort -u | xargs -d '\n' sha256sum > "$receipt/compiler-inputs-before.sha256"
execution=1
finish()
{
    local interrupted=$? closure=0
    trap - EXIT
    set +e
    for kind in source tools compiler-inputs; do
        sha256sum -c "$receipt/$kind-before.sha256" > "$receipt/$kind-check.log" 2>&1 || closure=1
    done
    git diff --binary > "$receipt/source-after.diff"
    git status --porcelain --untracked-files=all > "$receipt/status-after.txt"
    cmp "$receipt/source-before.diff" "$receipt/source-after.diff" || closure=1
    cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
    paths > "$receipt/tools-after.tsv" || closure=1
    cmp "$receipt/tools-before.tsv" "$receipt/tools-after.tsv" || closure=1
    config_state > "$receipt/checker-config-after.txt" || closure=1
    cmp "$receipt/checker-config-before.txt" "$receipt/checker-config-after.txt" || closure=1
    test "$(git rev-parse HEAD)" = 6faf053c28d7b33de30daeaeae8408340685d006 || closure=1
    printf '%s\n' "$execution" > "$receipt/execution.status"
    printf '%s\n' "$closure" > "$receipt/closure.status"
    if test "$execution" = 0 && test "$closure" = 0 && test "$interrupted" = 0; then
        printf '0\n' > "$receipt/aggregate.status"
        exit 0
    fi
    exit 1
}
trap finish EXIT
gate()
{
    local label=$1 status=0
    shift
    printf '%q ' "$@" > "$receipt/$label.command"
    printf '\n' >> "$receipt/$label.command"
    "$@" > "$receipt/$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" > "$receipt/$label.status"
    return "$status"
}
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for mode in o0 o2 asan ubsan; do
    flags=(-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -g -fno-pie -no-pie)
    case "$mode" in
    o0) flags+=(-O0) ;;
    o2) flags+=(-O2) ;;
    asan) flags+=(-O1 -fsanitize=address -fno-sanitize-recover=all) ;;
    ubsan) flags+=(-O1 -fsanitize=undefined -fno-sanitize-recover=all) ;;
    esac
    gate "$mode-compile" gcc "${flags[@]}" tests/efivar_unlock_test.c -o "$receipt/build/$mode"
    gate "$mode-contract" "$receipt/build/$mode" "$unsupported_syscall"
done
gate production-compile gcc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -O2 util/qemu/fixtures/linux-mini/efivar-unlock.c -o "$receipt/build/efivar-unlock"
gate old-compile gcc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -O0 \
    "-DCDK2_EFIVAR_UNLOCK_SOURCE=\"$receipt/old-source/efivar-unlock.c\"" tests/efivar_unlock_test.c -o "$receipt/build/old-contract"
old_status=0
gate old-contract "$receipt/build/old-contract" "$unsupported_syscall" || old_status=$?
test "$old_status" = 134
grep -Fq 'efivar_unlock_main(2, arguments) == 1' "$receipt/old-contract.log"
printf '0\n' > "$receipt/old-contract-opposition.status"
gate helper-checkpatch perl util/lint/checkpatch.pl --no-tree --show-types --file util/qemu/fixtures/linux-mini/efivar-unlock.c
gate fixture-checkpatch perl util/lint/checkpatch.pl --no-tree --show-types --file tests/efivar_unlock_test.c
execution=0
