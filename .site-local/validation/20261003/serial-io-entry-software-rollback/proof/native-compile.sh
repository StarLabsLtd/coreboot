#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
source_dir=/home/sean/Documents/.cdk2-worktrees/serial-io-entry-failure-context-after594
config_dir=/home/sean/serial-io-rollback-gate.MWFrOQ
proof_dir=/home/sean/serial-io-rollback-proof.OTmXWe
cd "$source_dir"
sha256sum -c "$proof_dir/native-pe-before.sha256"
native_compiler=$(sed -n 's/^CDK2_NATIVE_CC=//p' "$config_dir/native/command-inputs")
native_flags=$(sed -n 's/^CDK2_NATIVE_CFLAGS=//p' "$config_dir/native/command-inputs")
native_includes=$(sed -n 's/^CDK2_NATIVE_INCLUDES=//p' "$config_dir/native/command-inputs")
filtered_flags=
for flag in $native_flags; do
	case "$flag" in
	-fdata-sections|-ffunction-sections) ;;
	*) filtered_flags="$filtered_flags $flag" ;;
	esac
done
# Exact SerialIO compiler recipe, using actual named-gate command-inputs.
$native_compiler $filtered_flags -fno-ident -fcf-protection=none \
	-maccumulate-outgoing-args $native_includes \
	-c src/modules/serial_io/serial_io.c -o "$proof_dir/serial-io-native.o"
nm -u "$proof_dir/serial-io-native.o" > "$proof_dir/native-undefined.log"
test ! -s "$proof_dir/native-undefined.log"
sha256sum -c "$proof_dir/native-pe-before.sha256"
printf 'PASS native hardware-branch compile, no unresolved symbols; not PE/native execution\n'
