#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
source_dir=/home/sean/Documents/.cdk2-worktrees/serial-io-validated-tuples-after592
proof_dir=/home/sean/serial-io-final-proof.ChzR2V
cd "$source_dir"
sha256sum include/cdk2/uart.h include/cdk2/diagnostic.h src/boot/Makefile \
	src/boot/coreboot.h src/boot/coreboot_handoff.c src/boot/coreboot_test.c \
	src/lib/diagnostic.c src/modules/serial_io/serial_io.c tests/diagnostic_test.c \
	tests/serial_io_test.c util/lint/cdk2-typedefs.checkpatch \
	tests/checkpatch_filter_test.sh > "$proof_dir/source-before.sha256"
for profile in native retained; do
	case "$profile" in
	native) config_dir=/home/sean/serial-io-native-gate.wYokoR ;;
	retained) config_dir=/home/sean/serial-io-validated-gate.uqrNZM ;;
	esac
	sha256sum "$config_dir/resolved.config" "$config_dir/include/cdk2/config.h" \
		> "$proof_dir/$profile-config-before.sha256"
	for mode in o0 o2 san; do
		case "$mode" in
		o0) flags=-O0 ;;
		o2) flags=-O2 ;;
		san) flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' ;;
		esac
		cc -std=c11 -Wall -Wextra -Werror -fshort-wchar $flags \
			-I"$config_dir/include" -Iinclude tests/serial_io_test.c \
			-o "$proof_dir/$profile-serial-$mode"
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$proof_dir/$profile-serial-$mode"
		cc -std=c11 -Wall -Wextra -Werror -fshort-wchar $flags -pthread \
			-I"$config_dir/include" -Iinclude -DCDK2_DIAG_UNIT_TEST \
			tests/diagnostic_test.c src/lib/diagnostic.c \
			-o "$proof_dir/$profile-diagnostic-$mode"
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$proof_dir/$profile-diagnostic-$mode"
	done
	sha256sum -c "$proof_dir/$profile-config-before.sha256"
done
sha256sum -c "$proof_dir/source-before.sha256"
