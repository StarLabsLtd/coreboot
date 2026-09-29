#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	> "$temporary/include/config.h"

source_file="$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_route_composition.c"
common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_authvar_presence_route_composition_test.c
	$source_file"

for profile in o0 o2 asan ubsan; do
	case "$profile" in
	o0) flags=-O0 ;;
	o2) flags=-O2 ;;
	asan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address' ;;
	ubsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=undefined' ;;
	esac
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/$profile"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/$profile"
done

mutation()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"

	sed "$expression" "$source_file" > "$mutant"
	! cmp -s "$source_file" "$mutant"
	# shellcheck disable=SC2086
	${CC:-cc} $common -O2 \
		"$root/tests/lib/starbook_mtl_authvar_presence_route_composition_test.c" \
		"$mutant" -o "$temporary/$name"
	if "$temporary/$name" >/dev/null 2>&1; then
		echo "StarBook MTL presence route composition mutation survived: $name" >&2
		exit 1
	fi
}

mutation noncanonical-arm 's/&owner.arm, &owner.slot/NULL, \&owner.slot/'
mutation noncanonical-slot \
	'/smm_get_payload_mm_authvar_presence_transaction_slot/,/^}/s/return &owner.slot;/return NULL;/'

echo 'StarBook MTL authenticated-variable presence route composition owner tests: PASS'
