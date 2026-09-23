#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

for spec in 3:4096:4096 4:4096:4096 5:4096:4096 6:4096:4096 \
	7:4096:4096 8:4096:4096 3:4096:2048 4:4096:2048 \
	5:4096:2048 6:4096:2048 7:4096:2048 8:4096:2048 \
	3:8192:4096 3:8192:2048; do
	old_ifs=$IFS
	IFS=:
	set -- $spec
	IFS=$old_ifs
	blocks=$1
	block_size=$2
	erase_size=$3
	for optimization in 0 2; do
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
			-D__SMM__ -DTEST_BLOCK_COUNT="$blocks" \
			-DTEST_BLOCK_SIZE="$block_size" -DTEST_ERASE_SIZE="$erase_size" \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
			-I"$root/tests/lib" \
			"$root/tests/lib/payload_mm_authvar_executor_geometry_test.c" \
			"$root/src/lib/payload_mm_authvar.c" \
			"$root/src/lib/payload_mm_authvar_runtime.c" \
			"$root/src/lib/payload_mm_authvar_media.c" \
			"$root/src/lib/payload_mm_authvar_executor.c" \
			"$root/src/lib/payload_mm_authvar_ftw.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_writer.c" \
			-Wl,--wrap=payload_mm_authvar_media_fail_closed \
			-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
			-Wl,--wrap=payload_mm_authvar_media_cache_bind \
			-o "$tmp/geometry-b$blocks-s$block_size-e$erase_size-O$optimization"
		ASAN_OPTIONS=detect_leaks=1 \
			"$tmp/geometry-b$blocks-s$block_size-e$erase_size-O$optimization"
	done
done

printf '%s\n' 'Payload-MM authenticated-variable generated geometry: PASS'
