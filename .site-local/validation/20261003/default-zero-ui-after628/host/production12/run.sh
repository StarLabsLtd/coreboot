#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
root=/home/sean/Documents/.cdk2-worktrees/default-zero-timeout-hotkey-after626
output=/home/sean/default-zero-hotkey-proof.N3pnxd
p0=/home/sean/linear-boot-api-gate.f3YlpW
p1=/home/sean/normal-efi-disk-core-final.TJMaS8
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ulimit -s 65536
ulimit -c 0
cd "$root"
test "$(git rev-parse HEAD)" = ba707f924e9cebcb3411a9d6d38092735e6577ad
test -z "$(git status --porcelain)"
git diff --check
git diff ff1104483629cf6cb0cb18d462e18b5cb65e0c1d HEAD > "$output/candidate.patch"
git ls-files --stage | awk '$1 != "160000" {sub(/^[^\t]*\t/, ""); print}' |
  xargs -d '\n' sha256sum > "$output/source-before.sha256"
sha256sum "$output/run.sh" "$p0/resolved.config" "$p0/include/cdk2/config.h" \
  "$p1/resolved.config" "$p1/include/cdk2/config.h" /usr/bin/cc /usr/bin/objcopy \
  "$(cc -print-prog-name=cc1)" "$(command -v perl)" "$(command -v ld)" \
  "$(command -v bash)" "$(command -v awk)" "$(command -v xargs)" \
  "$(command -v sha256sum)" /usr/bin/time > "$output/inputs-before.sha256"
for symbol in PROTECTED_VARIABLE_RUNTIME NATIVE_SYSTEM_FMP; do
  grep -qx "#define CONFIG_CDK2_$symbol 0" "$p0/include/cdk2/config.h"
done
grep -qx '#define CONFIG_PAYLOAD_DMA_HANDOFF 0' "$p0/include/cdk2/config.h"
grep -qx '#define CONFIG_CDK2_LINEAR_BOOT 1' "$p0/include/cdk2/config.h"
for symbol in PROTECTED_VARIABLE_RUNTIME NATIVE_SYSTEM_FMP; do
  grep -qx "#define CONFIG_CDK2_$symbol 1" "$p1/include/cdk2/config.h"
done
mkdir "$output/keys"
keys=()
for key in "$root"/src/modules/bds/keys/*.der "$root"/src/modules/bds/keys/dbx_microsoft_update.bin; do
  filename=${key##*/}
  object="$output/keys/$filename.o"
  (cd "$root/src/modules/bds/keys" && objcopy -I binary -O elf64-x86-64 \
    -B i386:x86-64 --rename-section .data=.rodata.secure_boot_defaults,alloc,load,readonly,data,contents \
    "$filename" "$object")
  keys+=("$object")
done
sha256sum "${keys[@]}" > "$output/generated-keys-before.sha256"
sources=("$root/tests/bds_entry_test.c" "$root/src/modules/bds/model.c"
  "$root/src/modules/con_splitter/input.c" "$root/src/modules/usb_keyboard/model.c"
  "$root/src/lib/diagnostic.c" "$root/src/modules/bds/settings.c"
  "$root/src/modules/bds/secure_boot.c" "$root/src/lib/signature_database.c"
  "$root/src/modules/lvgl_setup/settings.c" "$root/src/modules/lvgl_setup/form.c")
for profile in p0 p1; do
  if test "$profile" = p0; then input=$p0; else input=$p1; fi
  mkdir "$output/$profile"
  for optimization in 0 2; do
    for variant in normal disabled disk; do
      flags=(-std=c11 -m64 -O"$optimization" -g -Wall -Wextra -Werror -fshort-wchar
        -fno-builtin -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all
        -fno-omit-frame-pointer -I"$input/include" -I"$root/include")
      if test "$variant" = disabled; then flags+=(-DCDK2_BDS_TEST_SETUP_DISABLED); fi
      if test "$variant" = disk; then flags+=(-DCDK2_BDS_TEST_CAPSULE_DISK); fi
      prefix="$output/$profile/$variant-o$optimization"
      cc "${flags[@]}" -M -MT closure "${sources[@]}" > "$prefix.dependencies.mk"
      perl - "${#sources[@]}" "$prefix.dependencies.mk" <<-'PERL' > "$prefix.dependencies.list"
	use strict;
	use warnings;
	my $expected = shift @ARGV;
	local $/;
	open my $input, '<', $ARGV[0] or die $!;
	my $body = <$input>;
	$body =~ s/\\\n/ /g;
	my $targets = ($body =~ s/^closure:\s*//mg);
	die "dependency target count" unless $targets == $expected;
	my %paths;
	for my $path (split /\s+/, $body) {
		next unless length $path;
		die "unsupported escaped dependency" if $path =~ /\\/;
		$paths{$path} = 1;
	}
	print "$_\n" for sort keys %paths;
PERL
      while IFS= read -r dependency; do sha256sum "$dependency"; done \
        < "$prefix.dependencies.list" > "$prefix.dependencies-before.sha256"
      printf '%q ' cc "${flags[@]}" "${sources[@]}" "${keys[@]}" -o "$prefix.bin" > "$prefix.argv"
      printf '\n' >> "$prefix.argv"
      cc "${flags[@]}" "${sources[@]}" "${keys[@]}" -o "$prefix.bin" > "$prefix.build.log" 2>&1
      /usr/bin/time -o "$prefix.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
        "$prefix.bin" > "$prefix.log" 2>&1
      sha256sum -c "$prefix.dependencies-before.sha256" > "$prefix.dependencies-after.log"
    done
  done
done
sha256sum -c "$output/source-before.sha256" > "$output/source-after.log"
sha256sum -c "$output/inputs-before.sha256" > "$output/inputs-after.log"
sha256sum -c "$output/generated-keys-before.sha256" > "$output/generated-keys-after.log"
git diff --check
echo 'Two genuine profile headers, three modeled BDS variants, O0/O2 strict sanitizer: PASS'
