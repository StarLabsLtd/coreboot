#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

build_and_run()
{
	authvar=$1
	opal=$2
	name="authvar-$authvar-opal-$opal"
	mkdir -p "$temporary/$name/include"
	{
		printf '%s\n' \
			'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
			'#define CONFIG_FATAL_ASSERTS 0' \
			'#define CONFIG_MAX_CPUS 4' \
			'#define CONFIG_SMM_ASEG 0' \
			'#define CONFIG_SMM_TSEG 1' \
			'#define CONFIG_SMM_RESERVED_SIZE 0x20000' \
			'#define CONFIG_IED_REGION_SIZE 0x10000' \
			"#define CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM $authvar" \
			"#define CONFIG_SMM_OPAL_S3_STATE_SMRAM $opal"
		if [ "$authvar" -eq 1 ]; then
			printf '%s\n' '#define CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM_SIZE 0x2000'
		else
			printf '%s\n' '#define CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM_SIZE 0x0'
		fi
		if [ "$opal" -eq 1 ]; then
			printf '%s\n' '#define CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE 0x1000'
		else
			printf '%s\n' '#define CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE 0x0'
		fi
	} > "$temporary/$name/include/config.h"

	${CC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion \
		-Wno-shadow -Wno-unused-parameter \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/$name/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/3rdparty/vboot/firmware/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/cpu/x86/smm_persistent_subregion_test.c" \
		"$root/src/cpu/x86/smm/tseg_region.c" -o "$temporary/$name/test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/$name/test"
}

build_and_run 0 0
build_and_run 0 1
build_and_run 1 0
build_and_run 1 1
