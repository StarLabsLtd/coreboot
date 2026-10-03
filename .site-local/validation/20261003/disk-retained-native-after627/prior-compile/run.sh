#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=/home/sean/Documents/.cdk2-worktrees/disk-retained-four-epoch-observer-after623
producer=/home/sean/Documents/.coreboot-worktrees/capsule-disk-delivery-capability-after388
output=/home/sean/disk-retained-codec-compile-final.wqeSTE
sources=
for implementation in fv store record store_semantics writer default_store ftw; do
 sources="$sources $producer/src/lib/payload_mm_authvar_$implementation.c"
done
sources="$sources $producer/src/lib/uuid.c $producer/src/lib/hexstrtobin.c"
translation_units=1
# shellcheck disable=SC2086
for source in $sources; do translation_units=$((translation_units + 1)); done
set -- -std=gnu11 -Wall -Wextra -Werror -g -fno-builtin -fno-pie -no-pie \
 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -DENV_TEST=1 \
 -Du8=uint8_t -Du16=uint16_t -Du32=uint32_t -Du64=uint64_t -Ds32=int32_t -Ds64=int64_t \
 -include "$producer/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
 -I"$producer/src/commonlib/include" -I"$producer/src/commonlib/bsd/include" \
 -idirafter "$producer/src/include"
for optimization in 0 2; do
 # shellcheck disable=SC2086
 /usr/bin/cc "$@" -O"$optimization" -M -MT closure \
  "$root/tests/system_fmp_disk_retained_store.c" $sources > "$output/dependencies-o$optimization.mk"
done
perl - "$((translation_units * 2))" "$output/dependencies-o0.mk" "$output/dependencies-o2.mk" <<-'PERL' > "$output/dependencies.list"
	use strict;
	use warnings;
	my $expected = shift @ARGV;
	my $targets = 0;
	my %paths;
	for my $file (@ARGV) {
		open my $input, '<', $file or die $!;
		local $/;
		my $body = <$input>;
		$body =~ s/\\\n/ /g;
		$targets += ($body =~ s/^closure:\s*//mg);
		for my $path (split /\s+/, $body) {
			next unless length $path;
			die "escaped path" if $path =~ /\\/;
			$paths{$path} = 1;
		}
	}
	die "target cardinality $targets != $expected" unless $targets == $expected;
	print "$_\n" for sort keys %paths;
PERL
while IFS= read -r path; do sha256sum "$path"; done \
 < "$output/dependencies.list" > "$output/inputs-before.sha256"
sha256sum "$output/run.sh" "$root/tests/system_fmp_disk_retained_store_test.sh" \
 /usr/bin/cc /home/sean/normal-efi-disk-core-final.TJMaS8/include/cdk2/config.h \
 >> "$output/inputs-before.sha256"
for optimization in 0 2; do
 # shellcheck disable=SC2086
 /usr/bin/cc "$@" -O"$optimization" "$root/tests/system_fmp_disk_retained_store.c" \
  $sources -o "$output/check-o$optimization" > "$output/build-o$optimization.log" 2>&1
done
sha256sum -c "$output/inputs-before.sha256" > "$output/inputs-after-check.log"
printf '%s\n' 'Actual final codec O0/O2 compile-only PASS; no media/model execution'
