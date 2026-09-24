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
	'#ifndef CONFIG_SPI_FLASH_VOLATILE_LEASE' \
	'#define CONFIG_SPI_FLASH_VOLATILE_LEASE 1' \
	'#endif' \
	> "$temporary/include/config.h"

compile_test()
{
	compile_name=$1
	compile_source=$2
	shift 2
	compile_output="$temporary/$compile_name"
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wshadow \
		-Wno-sign-compare -Wstrict-prototypes -fno-builtin \
		-ffunction-sections -fdata-sections -pthread "$@" \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/drivers/spi" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/drivers/spi_flash_volatile_lease-test.c" \
		"$compile_source" -Wl,--gc-sections -o "$compile_output"
}

run_test()
{
	run_name=$1
	shift
	compile_test "$run_name" "$root/src/drivers/spi/spi_flash.c" "$@"
	"$temporary/$run_name"
}

mutant()
{
	mutant_name=$1
	mutant_expression=$2
	mutant_source="$temporary/$mutant_name.c"
	mutant_binary="$temporary/mutant-$mutant_name"
	cp "$root/src/drivers/spi/spi_flash.c" "$mutant_source"
	perl -0pi -e "$mutant_expression" "$mutant_source"
	cmp -s "$mutant_source" "$root/src/drivers/spi/spi_flash.c" && {
		printf '%s\n' "mutant $mutant_name did not modify its source" >&2
		exit 1
	}
	compile_test "mutant-$mutant_name" "$mutant_source" -O2
	if "$mutant_binary" >/dev/null 2>&1; then
		printf '%s\n' "mutant $mutant_name survived" >&2
		exit 1
	fi
	printf '%s\n' "mutant $mutant_name: rejected"
}

run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test thread-O1 -O1 -g -fno-omit-frame-pointer -fsanitize=thread
run_test disabled-O2 -O2 -DCONFIG_SPI_FLASH_HAS_VOLATILE_GROUP=0

mutant permit-peer-read \
	's/volatile_state[.]lease_state == VOLATILE_LEASE_IDLE/volatile_state.lease_state == VOLATILE_LEASE_ACTIVE/'
mutant permit-peer-group \
	's/if \(volatile_state[.]boundary \|\|\n\t    volatile_state[.]lease_state != VOLATILE_LEASE_IDLE\)/if (false)/'
mutant ignore-owner-mutation \
	's/!volatile_lease_handle_valid\(volatile_state[.]owner\)/false/g'
mutant ignore-callback-mutation \
	's/!volatile_lease_callbacks_valid\(\)/false/g'
mutant permit-second-lease \
	's/if \(volatile_state[.]lease_state != VOLATILE_LEASE_IDLE \|\|/if (false ||/'
mutant permit-one-past-span \
	's/offset <= flash->size/true/'
mutant overflow-prone-span \
	's/len <= flash->size - offset/offset + len <= flash->size/'

printf '%s\n' 'SPI flash volatile-lease tests: PASS'
