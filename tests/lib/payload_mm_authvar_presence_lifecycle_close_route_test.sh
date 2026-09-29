#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/lib -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include -no-pie"
sources="$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_route_test.c
	$root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c
	$root/src/lib/payload_mm_authvar_presence_lifecycle_close_route.c"

for profile in o0 o2 asan ubsan tsan; do
	case "$profile" in
	o0) flags=-O0 ;;
	o2) flags=-O2 ;;
	asan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address' ;;
	ubsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=undefined' ;;
	tsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=thread -Wno-tsan' ;;
	esac
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/$profile"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	TSAN_OPTIONS=halt_on_error=1 \
		"$temporary/$profile"
done

route_source="$root/src/lib/payload_mm_authvar_presence_lifecycle_close_route.c"
mutant()
{
	name=$1
	expression=$2
	mutated="$temporary/$name.c"

	sed "$expression" "$route_source" > "$mutated"
	if cmp -s "$route_source" "$mutated"; then
		echo "lifecycle-close route mutation changed nothing: $name" >&2
		exit 1
	fi
	mutant_sources=$(printf '%s\n' "$sources" | sed "s|$route_source|$mutated|")
	# shellcheck disable=SC2086
	${CC:-cc} $common -O2 $mutant_sources -o "$temporary/$name"
	if "$temporary/$name" >/dev/null 2>&1; then
		echo "lifecycle-close route mutation survived: $name" >&2
		exit 1
	fi
}

mutant unsealed-active-ticket \
	's/!memcmp(ticket, \&route->sealed_active_ticket, sizeof(\*ticket))/true/'
mutant terminal-after-public-source \
	'/scrub(\&route->sealed_active_ticket/,$s/ROUTE_IDLE/ROUTE_POISONED/'

echo 'Payload-MM authenticated-variable presence lifecycle-close route tests: PASS'
