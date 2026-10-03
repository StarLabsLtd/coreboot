#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
task_source=/home/sean/Documents/cdk2
task_output=/home/sean/legacy-signature-result-peer.bkYvnA
cd "$task_source"
git rev-parse HEAD > "$task_output/source-commit.txt"
sha256sum src/modules/system_fmp/transport.c \
	tests/system_fmp_transport_client_test.c \
	include/cdk2/system_fmp_transport.h src/modules/qemu_test_fmp/sha256.c \
	src/boot/coreboot.c > "$task_output/source-before.sha256"
for task_mode in O0 O2 sanitized; do
	case "$task_mode" in
	O0) task_flags='-O0' ;;
	O2) task_flags='-O2' ;;
	sanitized) task_flags='-O1 -g -fno-pie -no-pie -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' ;;
	esac
	# Intentional splitting of the fixed compiler option list.
	cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
		-Wconversion -Wsign-conversion -I"$task_source/include" \
		-I"$task_source/src/boot" $task_flags -c \
		"$task_output/client-test.c" -o "$task_output/client-$task_mode.o"
	cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
		-Wconversion -Wsign-conversion -I"$task_source/include" \
		-I"$task_source/src/boot" $task_flags -c \
		"$task_source/src/modules/system_fmp/transport.c" \
		-o "$task_output/transport-$task_mode.o"
	for task_unit in sha256 coreboot; do
		case "$task_unit" in
		sha256) task_input="$task_source/src/modules/qemu_test_fmp/sha256.c" ;;
		coreboot) task_input="$task_source/src/boot/coreboot.c" ;;
		esac
		cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
			-I"$task_source/include" -I"$task_source/src/boot" \
			$task_flags -c "$task_input" -o "$task_output/$task_unit-$task_mode.o"
	done
	cc "$task_output/client-$task_mode.o" "$task_output/transport-$task_mode.o" \
		"$task_output/sha256-$task_mode.o" "$task_output/coreboot-$task_mode.o" \
		-Wl,--gc-sections $task_flags -o "$task_output/client-$task_mode"
	ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		"$task_output/client-$task_mode"
	printf 'Legacy real transport rejects result 2/status 1 without authenticated outcome: %s PASS\n' "$task_mode"
done
sha256sum -c "$task_output/source-before.sha256"
