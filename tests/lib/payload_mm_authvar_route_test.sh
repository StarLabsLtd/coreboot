#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

for optimization in 0 2; do
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_route_test.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		-o "$tmp/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization"
done

check_mutant()
{
	name="$1"
	mutant="$2"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_route.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_route_test.c" "$mutant" \
			-o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant="$tmp/route-name.c"
sed '0,/name_is(request, pk_name/s//name_is(request, kek_name/' \
	"$root/src/lib/payload_mm_authvar_route.c" > "$mutant"
check_mutant exact-name "$mutant"

mutant="$tmp/route-order.c"
sed 's/PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_KEK;/PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;/' \
	"$root/src/lib/payload_mm_authvar_route.c" > "$mutant"
check_mutant authority-order "$mutant"

mutant="$tmp/route-attrs.c"
sed 's/(request->attributes \& required) != required/(request->attributes \& required) == required/' \
	"$root/src/lib/payload_mm_authvar_route.c" > "$mutant"
check_mutant secure-attributes "$mutant"

printf '%s\n' 'Payload-MM authenticated-variable routing tests: PASS'
