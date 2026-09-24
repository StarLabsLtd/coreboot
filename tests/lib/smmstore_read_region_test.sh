#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 1' \
	'#define CONFIG_SMMSTORE_BLOCK_SIZE 65536' \
	> "$temporary/config.h"
printf '%s\n' \
	'#define FMAP_SECTION_SMMSTORE_START 0' \
	'#define FMAP_SECTION_SMMSTORE_SIZE 0x80000' \
	> "$temporary/fmap_config.h"

common_flags="-std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections
-D__TEST__ -D__COREBOOT__ -D__SMM__ -fno-builtin
-include $root/src/include/kconfig.h -include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary -I$root/src -I$root/src/include -I$root/src/commonlib/include
-I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include"

build_and_run()
{
	name=$1
	optimization=$2
	mkdir -p "$temporary/$name"

	# shellcheck disable=SC2086
	cc $common_flags "$optimization" -fsanitize=address,undefined -c \
		"$root/src/drivers/smmstore/read_region.c" \
		-o "$temporary/$name/read_region.o"
	relocations=$(objdump -r -j .text.smmstore_lookup_read_region \
		"$temporary/$name/read_region.o")
	printf '%s\n' "$relocations" | grep -q boot_device_ro_subregion
	! printf '%s\n' "$relocations" | \
		grep -Eq 'boot_device_rw|lookup_store_region'
	# shellcheck disable=SC2086
	cc $common_flags "$optimization" -fsanitize=address,undefined \
		"$root/tests/lib/smmstore_read_region_test.c" \
		"$temporary/$name/read_region.o" -Wl,--gc-sections \
		-o "$temporary/$name/test"

	"$temporary/$name/test"
	! nm -u "$temporary/$name/test" | grep -q 'boot_device_rw'
	! nm "$temporary/$name/read_region.o" | \
		grep -Eq 'smmstore_(preprocess_cmd|lookup_region)|boot_device_rw'
}

build_and_run debug -O0
build_and_run optimized -O2

printf '%s\n' 'SMMSTORE read-only region tests: PASS'
