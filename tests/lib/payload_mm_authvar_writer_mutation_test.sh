#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

compile_and_reject()
{
	name="$1"
	shift
	sed "$@" "$root/src/lib/payload_mm_authvar_writer.c" > "$tmp/$name.c"
	cc -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_writer_test.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$tmp/$name.c" -o "$tmp/$name"
	if ASAN_OPTIONS=detect_leaks=1 "$tmp/$name" >/dev/null 2>&1; then
		printf 'mutation survived: %s\n' "$name" >&2
		exit 1
	fi
}

compile_and_reject direct_added \
	'0,/PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY)/s//PAYLOAD_MM_AUTHVAR_STATE_ADDED)/'
compile_and_reject persist_append \
	's/source->attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE/source->attributes/'
compile_and_reject reverse_timestamp \
	's/source->timestamp) > 0/source->timestamp) < 0/'
compile_and_reject skip_transition \
	'0,/old_state & PAYLOAD_MM_AUTHVAR_STATE_IN_DELETED_TRANSITION/s//old_state/'

printf '%s\n' 'Payload-MM authenticated-variable writer mutation tests: PASS'
