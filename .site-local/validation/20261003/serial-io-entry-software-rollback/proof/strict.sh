#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
source_dir=/home/sean/Documents/.cdk2-worktrees/serial-io-entry-failure-context-after594
proof_dir=/home/sean/serial-io-rollback-proof.OTmXWe
cd "$source_dir"
sha256sum src/modules/serial_io/serial_io.c tests/serial_io_test.c include/cdk2/uart.h \
	> "$proof_dir/source-before.sha256"
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
			-o "$proof_dir/$profile-$mode"
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$proof_dir/$profile-$mode"
		printf 'PASS %s %s actual entry software rollback\n' "$profile" "$mode"
	done
	sha256sum -c "$proof_dir/$profile-config-before.sha256"
done
sha256sum -c "$proof_dir/source-before.sha256"
