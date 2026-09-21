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
	'#define CONFIG_ROM_SIZE (64U * 1024U)' \
	'#define CONFIG_MAINBOARD_VENDOR "Star Labs"' \
	'#define CONFIG_MAINBOARD_PART_NUMBER "StarBook Mk VII"' > \
	"$temporary/include/config.h"
printf '%s\n' \
	'#define FMAP_SECTION_FLASH_START 0' \
	'#define FMAP_SECTION_FLASH_SIZE (64U * 1024U)' > \
	"$temporary/include/fmap_config.h"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin -pthread "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/lib" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/capsule_platform_adapter_test.c" \
		"$root/src/lib/capsule_platform_adapter.c" \
		"$root/src/lib/identity.c" -o "$temporary/$name"
	"$temporary/$name"
}

run_test ordinary -fstack-usage
run_test optimized -O2
run_test strict -O2 -Wconversion -Wsign-conversion
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all

max_stack=$(awk '/capsule_platform_adapter.c:/ { if ($2 > max) max = $2 } \
	END { print max + 0 }' "$temporary"/*.su)
test "$max_stack" -le 2048
printf '%s\n' \
	"Capsule platform adapters O0/O2/strict/ASan+UBSan; max stack ${max_stack} bytes: PASS"
