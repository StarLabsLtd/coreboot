#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_MAX_CPUS 64' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	> "$temporary/include/config.h"
for optimization in 0 2; do
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-O"$optimization" -no-pie -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/mainboard/starlabs/starbook_mtl_presence_bootstrap_sender_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_sender.c" \
		-o "$temporary/test"
	"$temporary/test" 0
	"$temporary/test" 1
	for failure in $(seq 2 16); do
		result=0
		"$temporary/test" "$failure" || result=$?
		[ "$result" -eq 77 ] || exit 1
	done
done
echo 'MTL bootstrap actual sender disabled/bounds/hostile ACK tests: PASS'
