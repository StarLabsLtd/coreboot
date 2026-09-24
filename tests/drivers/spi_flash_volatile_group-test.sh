#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_FATAL_ASSERTS 0' \
	'#define CONFIG_BOOT_DEVICE_SPI_FLASH_BUS 0' \
	'#define CONFIG_ROM_SIZE (16U * 1024U * 1024U)' \
	'#ifndef CONFIG_SPI_FLASH_HAS_VOLATILE_GROUP' \
	'#define CONFIG_SPI_FLASH_HAS_VOLATILE_GROUP 1' \
	'#endif' \
	> "$temporary/include/config.h"

compile_test()
{
	name=$1
	source=$2
	shift 2
	output="$temporary/$name"
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wshadow -Wno-sign-compare \
		-Wstrict-prototypes -fno-builtin -ffunction-sections \
		-fdata-sections "$@" -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/drivers/spi" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/drivers/spi_flash_volatile_group-test.c" \
		"$source" -Wl,--gc-sections -o "$output"
}

run_test()
{
	name=$1
	shift
	compile_test "$name" "$root/src/drivers/spi/spi_flash.c" "$@"
	"$temporary/$name"
}

mutant()
{
	name=$1
	expression=$2
	source="$temporary/$name.c"
	cp "$root/src/drivers/spi/spi_flash.c" "$source"
	perl -0pi -e "$expression" "$source"
	cmp -s "$source" "$root/src/drivers/spi/spi_flash.c" && {
		printf '%s\n' "mutant $name did not modify its source" >&2
		exit 1
	}
	compile_test "mutant-$name" "$source" -O2
	if "$temporary/mutant-$name" >/dev/null 2>&1; then
		printf '%s\n' "mutant $name survived" >&2
		exit 1
	fi
}

run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test disabled-O2 -O2 -DCONFIG_SPI_FLASH_HAS_VOLATILE_GROUP=0

mutant inverted-underflow \
	's/assert\(count != 0\);/assert(count == 0);/'
mutant removed-underflow \
	's/\tassert\(count != 0\);\n\tif \(count == 0\)\n\t\treturn -1;\n//'
mutant failed-begin-increments \
	's/\t\tif \(ret\)\n\t\t\treturn ret;\n//'
mutant missing-overflow \
	's/\tif \(count == UINT32_MAX\)\n\t\treturn -1;\n//'
mutant premature-nested-end \
	's/\tif \(count > 1\) \{\n\t\tvolatile_group_count = count - 1;\n\t\treturn 0;\n\t\}\n//'
mutant retained-failed-end-ownership \
	's/\tvolatile_group_count = 0;\n\n\treturn ret;/\tif (!ret)\n\t\tvolatile_group_count = 0;\n\n\treturn ret;/'

printf '%s\n' 'SPI flash volatile-group state tests: PASS'
