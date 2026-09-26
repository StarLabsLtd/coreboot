#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

compile()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_presence_abi_test.c" \
		"$root/src/lib/payload_mm_authvar_presence.c" \
		-o "$temporary/$name"
	ASAN_OPTIONS=detect_leaks=1 "$temporary/$name"
}

compile strict-O0 -O0 -fstrict-aliasing -Wpedantic -Wconversion -Wshadow
compile strict-O2 -O2 -fstrict-aliasing -Wpedantic -Wconversion -Wshadow
compile sanitized-O0 -O0 -g -fno-omit-frame-pointer -fstrict-aliasing \
	-Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined \
	-fno-sanitize-recover=all
compile sanitized-O2 -O2 -g -fno-omit-frame-pointer -fstrict-aliasing \
	-Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined \
	-fno-sanitize-recover=all

for machine in 32 64; do
	"${CC:-cc}" -m"$machine" -std=gnu11 -Wall -Wextra -Werror -fno-builtin \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-c "$root/tests/lib/payload_mm_authvar_presence_layout_dump.c" \
		-o "$temporary/layout-$machine.o"
done

compile_and_kill()
{
	name=$1
	expression=$2
	mutant="$temporary/presence-$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_presence.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_presence.c"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$temporary/mutant-$name-O$optimization"
		"${CC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wpedantic -Wconversion -Wshadow -fno-builtin \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_presence_abi_test.c" \
			"$mutant" -o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

compile_and_kill endpoint-reserved \
	's/endpoint->reserved)/false)/'
compile_and_kill request-reserved \
	's/request->reserved ||/false ||/'
compile_and_kill capability-required \
	's/return value != 0;/return true;/'
compile_and_kill response-echo \
	's/response_frame->request_id != request_frame->request_id ||/false ||/'
compile_and_kill response-reserved \
	's/sizeof(request_frame->capability)) || response_frame->reserved ||/sizeof(request_frame->capability)) || false ||/'
compile_and_kill status-domain \
	's/\tdefault:/\tcase PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ERROR_BIT:\n\t\treturn true;\n\tdefault:/'

if grep -Eq '(^|[^A-Za-z0-9_])(smram|store_offset|boot_media|flash_offset|block_id|spi_address|variable_name|vendor_guid|data_size)([^A-Za-z0-9_]|$)' \
	"$root/src/include/boot/payload_mm_authvar_presence.h"; then
	printf '%s\n' 'presence ABI exposes variable or private authority data' >&2
	exit 1
fi
if grep -R -Fq 'payload_mm_authvar_presence.c' \
	"$root/src"/*/Makefile.mk "$root/src"/Makefile.mk 2>/dev/null; then
	printf '%s\n' 'presence ABI has a firmware caller' >&2
	exit 1
fi

printf '%s\n' 'Payload-MM authenticated-variable presence ABI tests: PASS'
