#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 4' \
	'#define CONFIG_ROM_SIZE 0x10000' \
	'#define CONFIG_NO_FMAP_CACHE 1' \
	'#define CONFIG_CBFS_VERIFICATION 0' > "$temporary/include/config.h"
printf '%s\n' \
	'#define FMAP_OFFSET 0x100' \
	'#define FMAP_SIZE 0x1000' \
	'#define FMAP_SECTION_FLASH_START 0' \
	'#define FMAP_SECTION_FLASH_SIZE 0x10000' > \
	"$temporary/include/fmap_config.h"
printf '%s\n' \
	'#include <stddef.h>' \
	'typedef int vb2_error_t;' \
	'#define VB2_SUCCESS 0' \
	'static inline vb2_error_t metadata_hash_verify_fmap(const void *p, size_t s)' \
	'{ (void)p; (void)s; return VB2_SUCCESS; }' > \
	"$temporary/include/metadata_hash.h"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -Wno-sign-compare -Wno-unused-parameter "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/tests/include" \
		"$root/tests/lib/fmap_inventory_test.c" "$root/src/lib/fmap.c" \
		-Wl,--gc-sections -o "$temporary/$name"
	for case in valid nested exact-span partial duplicate-name flags \
		overflow count short; do
		"$temporary/$name" "$case"
	done
}

run_test ordinary
run_test optimized -O2
run_test strict -O2 -Wconversion -Wsign-conversion
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
printf '%s\n' 'FMAP inventory O0/O2/strict/ASan+UBSan hostile cases: PASS'
