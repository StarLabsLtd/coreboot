#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
proof_dir=/home/sean/uart-runtime-callback-peer.CIJmN3
source_dir=/home/sean/Documents/.cdk2-worktrees/validated-uart-after591
profile_dir=/home/sean/validated-uart-gate.INSDAY
cd "$source_dir"
sha256sum include/cdk2/diagnostic.h src/lib/diagnostic.c \
	tests/diagnostic_runtime_transition_test.c > "$proof_dir/source-before.sha256"
sha256sum "$profile_dir/resolved.config" "$profile_dir/include/cdk2/config.h" \
	> "$proof_dir/config-current-before.sha256"
for mode in o0 o2 san; do
	case "$mode" in
	o0) flags=-O0 ;;
	o2) flags=-O2 ;;
	san) flags='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie' ;;
	esac
	cc -std=c11 -Wall -Wextra -Werror -fshort-wchar $flags \
		-I"$profile_dir/include" -Iinclude -DCDK2_DIAG_UNIT_TEST \
		tests/diagnostic_runtime_transition_test.c src/lib/diagnostic.c \
		-o "$proof_dir/runtime-$mode"
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1 "$proof_dir/runtime-$mode"
done
sha256sum -c "$proof_dir/source-before.sha256"
sha256sum -c "$proof_dir/config-current-before.sha256"
