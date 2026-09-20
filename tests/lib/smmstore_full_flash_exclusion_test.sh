#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

build_and_run()
{
	name=$1
	full_flash=$2
	mkdir -p "$temporary/$name"
	printf '%s\n' \
		'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
		'#define CONFIG_MAX_CPUS 1' \
		'#define CONFIG_SMMSTORE_BLOCK_SIZE 65536' \
		"#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS $full_flash" \
		> "$temporary/$name/config.h"
	printf '%s\n' \
		'#define FMAP_SECTION_SMMSTORE_START 0' \
		'#define FMAP_SECTION_SMMSTORE_SIZE 0x80000' \
		> "$temporary/$name/fmap_config.h"
	cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections \
		-fdata-sections -D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-fno-builtin -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/$name" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -c \
		"$root/src/drivers/smmstore/store.c" \
		-o "$temporary/$name/store.o"
	cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections \
		-fdata-sections -D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-fno-builtin -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/$name" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/smmstore_full_flash_exclusion_test.c" \
		"$temporary/$name/store.o" -Wl,--gc-sections \
		-o "$temporary/$name/test"
	"$temporary/$name/test"
}

build_and_run legacy 1
build_and_run typed 0
nm -u "$temporary/legacy/store.o" | grep -q ' U boot_device_rw$'
! nm -u "$temporary/typed/store.o" | grep -q ' U boot_device_rw$'

configure()
{
	name=$1
	shift
	mkdir -p "$temporary/config-$name" "$temporary/build-$name"
	cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" \
		"$temporary/config-$name/.config"
	printf '%s\n' \
		'CONFIG_SMMSTORE=y' \
		'CONFIG_DRIVERS_EFI_VARIABLE_STORE=y' \
		'CONFIG_DRIVERS_EFI_FW_INFO=y' \
		'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y' \
		"$@" >> "$temporary/config-$name/.config"
	make -C "$root" obj="$temporary/build-$name" \
		DOTCONFIG="$temporary/config-$name/.config" olddefconfig >/dev/null
}

configure legacy
grep -qx 'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y' \
	"$temporary/config-legacy/.config"
grep -qx 'CONFIG_SMMSTORE_FULL_FLASH_ACCESS=y' \
	"$temporary/config-legacy/.config"

configure typed 'CONFIG_Q35_CAPSULE_BROKER_TEST_PROOF=y'
grep -qx 'CONFIG_Q35_CAPSULE_BROKER_TEST_PROOF=y' \
	"$temporary/config-typed/.config"
grep -qx 'CONFIG_CAPSULE_BROKER_CONTRACT=y' \
	"$temporary/config-typed/.config"
grep -qx 'CONFIG_SMMSTORE=y' "$temporary/config-typed/.config"
! grep -q '^CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y$' \
	"$temporary/config-typed/.config"
! grep -q '^CONFIG_SMMSTORE_FULL_FLASH_ACCESS=y$' \
	"$temporary/config-typed/.config"

printf '%s\n' 'SMMSTORE full-flash exclusion tests: PASS'
