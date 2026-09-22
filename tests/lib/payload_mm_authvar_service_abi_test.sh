#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_service_abi_test.c" \
		"$root/src/lib/payload_mm_authvar_service.c" \
		-o "$temporary/$name"
	"$temporary/$name"
}

run_test strict-O0 -O0 -fstrict-aliasing -Wpedantic -Wconversion -Wshadow
run_test strict-O2 -O2 -fstrict-aliasing -Wpedantic -Wconversion -Wshadow
run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer -fstrict-aliasing \
	-Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined \
	-fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer -fstrict-aliasing \
	-Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined \
	-fno-sanitize-recover=all

if grep -Eq '(^|[^A-Za-z0-9_])(smram|store_offset|boot_media|flash_offset|block_id|spi_address)([^A-Za-z0-9_]|$)' \
	"$root/src/include/boot/payload_mm_authvar_service.h"; then
	printf '%s\n' 'public authenticated-variable wire exposes private authority' >&2
	exit 1
fi
if grep -R -Fq 'payload_mm_authvar_service.c' \
	"$root/src"/*/Makefile.mk "$root/src"/Makefile.mk 2>/dev/null; then
	printf '%s\n' 'authenticated-variable service ABI has a firmware caller' >&2
	exit 1
fi

printf '%s\n' 'Payload-MM authenticated-variable service ABI tests: PASS'
