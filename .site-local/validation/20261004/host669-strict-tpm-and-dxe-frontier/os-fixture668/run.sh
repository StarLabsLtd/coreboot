#!/bin/bash
set -euo pipefail
export PATH=/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin
export HOME=/home/sean LANG=C LC_ALL=C TZ=UTC PYTHONDONTWRITEBYTECODE=1
test -z "${PYTHONOPTIMIZE+x}"
receipt=/home/sean/tpm-source-owned-os-host-refreeze.Ktq6vZ
source=/home/sean/Documents/.cdk2-worktrees/tpm-os-fixture-after661
canonical=/home/sean/Documents/cdk2
pins=/home/sean/normal-linux-fwui-uki-after2f83.ha8wDM/pinned
fixture=$canonical/util/qemu/fixtures/tpm-telemetry-Ktq6vZ.raw
base=$canonical/util/qemu/fixtures/nvme-final-c4bec.raw
cd "$source"
test ! -e "$receipt/uki" && test ! -e "$fixture"
mkdir "$receipt/uki" "$receipt/tmp"
export TMPDIR=$receipt/tmp
printf '1\n' > "$receipt/aggregate.status"
tools=(bash sh git ssh-keygen gcc cc ld as cc1 cpio gzip objcopy objdump python3
	sha256sum cmp cut cat mkdir realpath install find sort touch stat mktemp grep
	rm mcopy tail xargs dd env sed awk readlink perl cp file dirname)
paths()
{
	local tool selected
	for tool in "${tools[@]}"; do
		if test "$tool" = cc1; then
			selected=$(/usr/bin/gcc -print-prog-name=cc1)
		else
			selected=$(type -P "$tool")
		fi
		printf '%s\t%s\n' "$tool" "$(readlink -f "$selected")"
	done
}
finish()
{
	local execution=$? closure=0
	trap - EXIT
	set +e
	printf '%s\n' "$execution" > "$receipt/execution.status"
	for kind in source tools pins compiler-inputs; do
		sha256sum --quiet -c "$receipt/$kind-before.sha256" \
			> "$receipt/$kind-after-check.log" 2>&1 || closure=1
	done
	git rev-parse HEAD > "$receipt/head-after.txt"
	git status --porcelain > "$receipt/status-after.txt"
	git diff --binary > "$receipt/source-after.diff"
	cmp "$receipt/head-before.txt" "$receipt/head-after.txt" || closure=1
	cmp "$receipt/status-before.txt" "$receipt/status-after.txt" || closure=1
	cmp "$receipt/frozen.diff" "$receipt/source-after.diff" || closure=1
	paths > "$receipt/tool-paths-after.tsv"
	cmp "$receipt/tool-paths-before.tsv" "$receipt/tool-paths-after.tsv" || closure=1
	git -C "$canonical" rev-parse HEAD > "$receipt/canonical-head-after.txt"
	git -C "$canonical" status --porcelain > "$receipt/canonical-status-after.txt"
	cmp "$receipt/canonical-head-before.txt" "$receipt/canonical-head-after.txt" || closure=1
	cmp "$receipt/canonical-status-before.txt" "$receipt/canonical-status-after.txt" || closure=1
	printf '%s\n' "$closure" > "$receipt/closure.status"
	if test "$execution" = 0 && test "$closure" = 0; then
		printf '0\n' > "$receipt/aggregate.status"
		exit 0
	fi
	exit 1
}
trap finish EXIT
git rev-parse HEAD > "$receipt/head-before.txt"
git status --porcelain > "$receipt/status-before.txt"
git diff --binary > "$receipt/frozen.diff"
test "$(cat "$receipt/head-before.txt")" = 6faf053c28d7b33de30daeaeae8408340685d006
test "$(sha256sum "$receipt/frozen.diff" | cut -d' ' -f1)" = 200741251067a5c46563fc5e2ea142b41790082edc3e8be764aff04d01c8b041
git verify-commit HEAD > "$receipt/signature.log" 2>&1
git -C "$canonical" rev-parse HEAD > "$receipt/canonical-head-before.txt"
git -C "$canonical" status --porcelain > "$receipt/canonical-status-before.txt"
test "$(cat "$receipt/canonical-head-before.txt")" = 6faf053c28d7b33de30daeaeae8408340685d006
test ! -s "$receipt/canonical-status-before.txt"
git -C "$canonical" verify-commit HEAD > "$receipt/canonical-signature.log" 2>&1
git -C "$canonical" check-ignore "$fixture/tpm-linux.raw" "$fixture/tpm-linux.manifest" \
	> "$receipt/ignored-fixture.txt"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
	while IFS= read -r path; do sha256sum "$source/$path"; done > "$receipt/source-before.sha256"
sha256sum "$pins/kernel" "$pins/busybox" "$pins/cbmem" "$pins/stub" "$base" \
	> "$receipt/pins-before.sha256"
paths > "$receipt/tool-paths-before.tsv"
sha256sum "$0" "$receipt/README.txt" /usr/bin/time > "$receipt/tools-before.sha256"
while IFS=$'\t' read -r name path; do sha256sum "$path"; done \
	< "$receipt/tool-paths-before.tsv" >> "$receipt/tools-before.sha256"
for name in collect2 lto-wrapper; do
	sha256sum "$(readlink -f "$(/usr/bin/gcc "-print-prog-name=$name")")" \
		>> "$receipt/tools-before.sha256"
done
for name in crt1.o crti.o crtn.o crtbeginT.o crtend.o libgcc.a libgcc_eh.a libc.a liblto_plugin.so; do
	selected=$(/usr/bin/gcc "-print-file-name=$name")
	test -f "$selected"
	sha256sum "$(readlink -f "$selected")" >> "$receipt/tools-before.sha256"
done
/usr/bin/gcc -M "$source/util/qemu/fixtures/linux-mini/efivar-unlock.c" \
	> "$receipt/actual-compiler-inputs.mk"
perl - "$receipt/actual-compiler-inputs.mk" <<'PERL' > "$receipt/compiler-inputs.list"
use strict;
use warnings;
open my $input, '<', $ARGV[0] or die $!;
local $/;
my $body = <$input>;
$body =~ s/\\\n/ /g;
$body =~ s/^[^:]+:\s*// or die 'missing compiler dependency target';
for my $path (split /\s+/, $body) {
    next unless length $path;
    die 'escaped compiler dependency' if $path =~ /\\/;
    print "$path\n";
}
PERL
while IFS= read -r path; do sha256sum "$path"; done \
	< "$receipt/compiler-inputs.list" > "$receipt/compiler-inputs-before.sha256"
stage()
{
	local label=$1 status=0
	shift
	printf '%q ' "$@" > "$receipt/$label.command"
	printf '\n' >> "$receipt/$label.command"
	/usr/bin/time -o "$receipt/$label.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
		"$@" > "$receipt/$label.log" 2>&1 || status=$?
	printf '%s\n' "$status" > "$receipt/$label.status"
	return "$status"
}
common=(env -i PATH="$PATH" HOME=/home/sean TMPDIR="$TMPDIR" LANG=C LC_ALL=C TZ=UTC
	PYTHONDONTWRITEBYTECODE=1)
stage generator-syntax bash -n util/qemu/bin/make-tpm-linux-fixture.sh
stage selftest-syntax bash -n util/qemu/bin/make-tpm-linux-fixture-selftest.sh
stage builder-refusals "${common[@]}" bash util/qemu/bin/build-local-mini-uki-selftest.sh \
	"$pins/kernel" "$pins/busybox" "$pins/cbmem" "$pins/stub"
stage build "${common[@]}" bash util/qemu/bin/build-local-mini-uki.sh \
	"$pins/kernel" "$pins/busybox" "$pins/cbmem" "$pins/stub" "$receipt/uki/tpm-linux.efi"
stage packaging-refusals "${common[@]}" bash util/qemu/bin/make-tpm-linux-fixture-selftest.sh \
	"$base" "$receipt/uki/tpm-linux.efi"
stage packaging "${common[@]}" bash util/qemu/bin/make-tpm-linux-fixture.sh \
	"$base" "$receipt/uki/tpm-linux.efi" "$fixture"
sha256sum "$receipt/uki/tpm-linux.efi" "$receipt/uki/tpm-linux.efi.manifest" \
	"$fixture/tpm-linux.raw" "$fixture/tpm-linux.manifest" > "$receipt/artifacts.sha256"
sha256sum --quiet -c "$receipt/artifacts.sha256" > "$receipt/artifacts-check.log"
