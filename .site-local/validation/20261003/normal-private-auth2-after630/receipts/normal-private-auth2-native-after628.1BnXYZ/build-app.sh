#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Build only a separately admitted terminal MAIN, not another firmware Core.
set -euo pipefail
stage=/home/sean/normal-private-auth2-native-after628.1BnXYZ
source=/home/sean/Documents/.cdk2-worktrees/normal-private-auth2-older-append-after628
old=/home/sean/native-default-zero-hotkey-after628.CfqEvm
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
# This is the real generated-config input used by the original normal628 Core
# build. The later ROM's own full.config is independently checked by run-native.
producer_config=/home/sean/disk-no-request-hex-producer-after619.pXSGcd/initial9/full.config
export PATH=/home/sean/.local/bin:/usr/local/bin:/usr/bin:/bin
export TMPDIR=/home/sean
ulimit -s 65536
ulimit -c 0
cd "$source"
test "$(git rev-parse HEAD)" = 4df1b4f47b3e36af7eb3ec2029d21c335d9dbbad
test -z "$(git status --porcelain)"
test "$(git -C "$producer" rev-parse HEAD)" = 7ee34bed989c46913c3ee6672fb25e83227c3b6c
test -z "$(git -C "$producer" status --porcelain)"
test ! -e "$stage/app"
mkdir "$stage/app"
cp -p "$old/resolved.config" "$stage/app/resolved.config"
git ls-files -z Makefile Kconfig src include tests configs | xargs -0 sha256sum \
	> "$stage/app/source-before.sha256"
(
	git -C "$producer" ls-files -z Kconfig Makefile Makefile.mk src/Kconfig \
		src/include src/commonlib util/kconfig 'src/**/Kconfig*' |
	while IFS= read -r -d '' path; do
		sha256sum "$producer/$path"
	done
) > "$stage/app/producer-before.sha256"
for dependency in bearssl lvgl; do
	path=/home/sean/Documents/cdk2/3rdparty/$dependency
	test "$(git -C "$path" rev-parse HEAD)" = "$(git ls-tree HEAD "3rdparty/$dependency" | awk '{print $3}')"
	test -z "$(git -C "$path" status --porcelain)"
	printf '%s %s\n' "$path" "$(git -C "$path" rev-parse HEAD)"
done > "$stage/app/vendor-heads.txt"
sha256sum "$stage/build-app.sh" "$stage/run-native.py" "$old/resolved.config" \
	"$old/include/cdk2/config.h" "$producer_config" /usr/bin/cc /usr/bin/make \
	> "$stage/app/inputs-before.sha256"
for tool in sh bash git perl openssl sha256sum xargs cp cmp dirname readlink ld as cat mkdir awk readelf objcopy; do
	sha256sum "$(command -v "$tool")"
done > "$stage/app/tools-before.sha256"
sha256sum "$(cc -print-prog-name=cc1)" >> "$stage/app/tools-before.sha256"
sha256sum /usr/bin/time >> "$stage/app/tools-before.sha256"
# These are the signed Makefile's exact default native flags and ten-source
# app vector; its two HOST PE helpers use the default HOST flag vector below.
# The reused real generated header is independently equal to the newly
# generated header after make; no firmware objects are reused in this build.
native_flags=(-ffreestanding -fno-builtin -fno-stack-protector -fpie -mcmodel=small
	-fvisibility=hidden -include "$source/include/cdk2/native_visibility.h"
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fdata-sections -ffunction-sections
	-fshort-wchar -m64 -mno-red-zone -mno-sse -mno-mmx -Os -Wall -Werror
	-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -fno-ident -fcf-protection=none
	-maccumulate-outgoing-args -I"$old/include" -I"$source/include")
app_sources=(tests/protected_variable_fullgraph_runtime_app.c
	src/modules/authvar_transport/owner.c src/modules/authvar_transport/transport.c
	src/modules/authvar_transport/native_x86.c src/lib/payload_mm_authvar_service.c
	src/lib/image_policy_snapshot.c src/boot/coreboot.c src/boot/coreboot_checksum.c
	src/boot/coreboot_resource.c src/lib/mem.c)
for path in "${app_sources[@]}"; do
	variant=()
	if [[ $path == tests/protected_variable_fullgraph_runtime_app.c ]]; then
		variant=(-DCDK2_NORMAL_PRIVATE_OLDER_APPEND=1)
	fi
	cc "${native_flags[@]}" "${variant[@]}" -M -MT closure "$source/$path"
done > "$stage/app/compile-closure.mk"
for path in util/pe_exec_sections.c util/native_pe_link.c; do
	cc -std=c11 -O2 -Wall -Wextra -Werror -fshort-wchar -M -MT closure "$source/$path"
done >> "$stage/app/compile-closure.mk"
perl - 12 "$stage/app/compile-closure.mk" <<-'PERL' > "$stage/app/compile-closure.list"
	use strict;
	use warnings;
	my ($expected, $file) = @ARGV;
	open my $input, '<', $file or die $!;
	local $/;
	my $body = <$input>;
	$body =~ s/\\\n/ /g;
	my $targets = ($body =~ s/^closure:\s*//mg);
	die "unexpected actual app/helper TU count" unless $targets == $expected;
	my %paths;
	for my $path (split /\s+/, $body) {
		next unless length $path;
		die "unsupported escaped dependency" if $path =~ /\\/;
		$paths{$path} = 1;
	}
	print "$_\n" for sort keys %paths;
PERL
while IFS= read -r path; do
	sha256sum "$path"
done < "$stage/app/compile-closure.list" > "$stage/app/compile-closure-before.sha256"
status=0
/usr/bin/time -o "$stage/app/build.time" -f 'WALL=%e USER=%U SYS=%S PEAK_KIB=%M EXIT=%x' \
	make --trace -j2 native-protected-variable-normal-older-append-app \
	CDK2_CONFIG="$stage/app/resolved.config" CDK2_BUILD_DIR="$stage/app" \
	COREBOOT_TREE="$producer" COREBOOT_CONFIG="$producer_config" \
	CDK2_BEARSSL_DIR=/home/sean/Documents/cdk2/3rdparty/bearssl \
	CDK2_LVGL_ROOT=/home/sean/Documents/cdk2/3rdparty/lvgl \
	> "$stage/app/build.log" 2>&1 || status=$?
printf '%s\n' "$status" > "$stage/app/outer.status"
sha256sum -c "$stage/app/source-before.sha256" > "$stage/app/source-after-check.log"
sha256sum -c "$stage/app/inputs-before.sha256" > "$stage/app/inputs-after-check.log"
sha256sum -c "$stage/app/tools-before.sha256" > "$stage/app/tools-after-check.log"
sha256sum -c "$stage/app/producer-before.sha256" > "$stage/app/producer-after-check.log"
sha256sum -c "$stage/app/compile-closure-before.sha256" > "$stage/app/compile-closure-after-check.log"
test -z "$(git status --porcelain)"
test -z "$(git -C "$producer" status --porcelain)"
for dependency in bearssl lvgl; do
	path=/home/sean/Documents/cdk2/3rdparty/$dependency
	test "$(git -C "$path" rev-parse HEAD)" = "$(git ls-tree HEAD "3rdparty/$dependency" | awk '{print $3}')"
	test -z "$(git -C "$path" status --porcelain)"
done
if test "$status" = 0; then
	cmp "$old/resolved.config" "$stage/app/resolved.config"
	cmp "$old/include/cdk2/config.h" "$stage/app/include/cdk2/config.h"
	sha256sum "$stage/app/native/NormalPrivateAuth2OlderAppend.efi" \
		"$stage/app/native/normal-private-auth2-inputs/private_certdb.bin" \
		"$stage/app/native/cdk2-pe-exec-sections" \
		"$stage/app/native/cdk2-native-pe-link" \
		> "$stage/app/outputs.sha256"
fi
cat "$stage/app/build.time"
exit "$status"
