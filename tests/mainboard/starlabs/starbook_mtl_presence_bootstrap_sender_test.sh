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
for service in 0 1; do
 printf '%s\n' "#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED $service" \
  >> "$temporary/include/config.h"
for flags in '-O0' '-O2' '-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		$flags -no-pie -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" -I"$root/src" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/mainboard/starlabs/starbook_mtl_presence_bootstrap_sender_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_sender.c" \
		"$root/src/lib/payload_mm_authvar_service.c" \
		-o "$temporary/test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$temporary/test" 0
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$temporary/test" 1
	if [ "$service" -eq 1 ]; then
	 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$temporary/test" 28
	fi
	for failure in $(seq 2 27); do
		result=0
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		 "$temporary/test" "$failure" || result=$?
		[ "$result" -eq 77 ] || exit 1
	done
done
 # Avoid redefining the configuration macro for the second composition.
 sed '/CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED/d' \
  "$temporary/include/config.h" > "$temporary/include/next-config.h"
 mv "$temporary/include/next-config.h" "$temporary/include/config.h"
done
echo 'MTL bootstrap actual sender disabled/bounds/hostile ACK tests: PASS'
