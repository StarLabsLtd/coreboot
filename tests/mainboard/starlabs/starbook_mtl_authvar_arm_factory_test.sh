#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 8' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
	'#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1' \
	> "$temporary/include/config.h"

build()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-ffunction-sections -fdata-sections -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/mainboard/starlabs/starbook_mtl_authvar_arm_factory_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_route_composition.c" \
		"$root/src/lib/payload_mm_authvar_presence_arm.c" \
		"$root/src/cpu/x86/smm_invocation_evidence.c" \
		"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
		-Wl,--gc-sections -o "$temporary/$name"
}

for optimization in 0 2; do
	build "bootstrap-O$optimization" -O"$optimization" \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=1 \
		-fsanitize=address,undefined -fno-sanitize-recover=all
	for fault in 0 1 2 3 4 5 6 7 8 9; do
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/bootstrap-O$optimization" "$fault"
	done
	build "ordinary-O$optimization" -O"$optimization" \
		-fsanitize=address,undefined -fno-sanitize-recover=all
	for fault in 0 1 6; do
		ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/ordinary-O$optimization" "$fault"
	done
done
printf '%s\n' 'Actual board arm initialization and claimed-BOOT admission: PASS'
