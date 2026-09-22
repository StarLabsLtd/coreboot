#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

ASAN_OPTIONS="detect_leaks=1${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
export ASAN_OPTIONS

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
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-Wstrict-prototypes -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_authvar_media_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_media.c" \
		-o "$temporary/$name"
	for mode in normal disjoint install-invalid install-unprotected-port \
		install-unprotected-context install-unprotected-callback bounds \
		fail-closed fail-closed-wrong-generation fail-closed-wrong-token \
		fail-closed-idle fail-closed-preinstall fail-closed-repeated \
		fail-closed-begin-callback \
		fail-closed-read-callback fail-closed-program-callback \
		fail-closed-erase-callback fail-closed-sync-callback \
		fail-closed-verify-callback fail-closed-sealed-sync-callback \
		fail-closed-end-callback \
		begin-zero begin-error begin-invalid \
		begin-reenter context-mutation read-short read-error read-invalid \
		read-reenter read-fail-closed \
		program-error-exact program-wp program-partial program-mutate \
		program-invalid program-reenter program-context-mutation program-sync \
		program-sync-reenter program-sync-context-mutation \
		program-postread erase-wp erase-error-exact erase-partial \
		erase-invalid erase-reenter erase-context-mutation erase-sync \
		erase-sync-context-mutation erase-postread end-error \
		end-reenter; do
		"$temporary/$name" "$mode"
	done
}

run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all

awk '/payload_mm_authvar_media[.]c/ && \
     $0 !~ /CONFIG_PAYLOAD_MM_AUTHVAR_MEDIA_PORT/ { bad = 1 } \
     END { exit bad }' "$root/src/lib/Makefile.mk"
printf '%s\n' 'Payload-MM authenticated-variable media hostile cases: PASS'
