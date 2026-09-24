#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-bundle.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" \
		-I"$root/src/commonlib/bsd/include" -idirafter "$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_bundle_test.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_store.c" -o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done
