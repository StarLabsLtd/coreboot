#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION 1' \
	> "$temporary/include/config.h"

source="$root/src/lib/payload_mm_authvar_presence_publication.c"
test_source="$root/tests/lib/payload_mm_authvar_presence_publication_test.c"
default_test_source="$root/tests/lib/payload_mm_authvar_presence_publication_default_test.c"

build_test()
{
	name=$1
	implementation=$2
	shift 2
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow \
		-fno-builtin -no-pie -pthread "$@" -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" "$test_source" "$implementation" \
		-o "$temporary/$name"
}

run_test()
{
	name=$1
	build_test "$@"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/$name"
}

run_test strict-O0 "$source" -O0
run_test strict-O2 "$source" -O2
run_test sanitized-O0 "$source" -O0 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test sanitized-O2 "$source" -O2 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all

test_source=$default_test_source
run_test default-inert-O0 "$source" -O0
run_test default-inert-O2 "$source" -O2
test_source="$root/tests/lib/payload_mm_authvar_presence_publication_test.c"

mutation()
{
	name=$1
	expression=$2
	mutant="$temporary/$name.c"

	sed "$expression" "$source" > "$mutant"
	if cmp -s "$mutant" "$source"; then
		echo "mutation changed nothing: $name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$name-O$optimization"
		if ! build_test "$binary" "$mutant" -O"$optimization" \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			>/dev/null 2>&1; then
			echo "mutation did not compile: $name O$optimization" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/$binary" >/dev/null 2>&1; then
			echo "mutation survived: $name O$optimization" >&2
			exit 1
		fi
	done
}

mutation ignore-required \
	's/required = platform_payload_mm_authvar_presence_required();/required = true;/'
mutation ignore-reserve-failure \
	's/payload_mm_authvar_presence_producer_reserve() != CB_SUCCESS/false/'
mutation ignore-constructor-failure \
	's/!platform_payload_mm_authvar_presence_composition(&composition)/false/'
mutation ignore-compose-failure \
	's/payload_mm_authvar_presence_producer_compose(&composition) !=/CB_SUCCESS !=/'
mutation ignore-take-failure \
	'/producer_publication_take(&receipt)/,+1s/CB_SUCCESS) {/CB_SUCCESS \&\& false) {/'
mutation no-abort \
	's/payload_mm_authvar_presence_producer_abort();/(void)publication_state;/'
mutation take-before-reservation \
	'/record = (void \*)lb_new_record(header);/i\
\t(void)payload_mm_authvar_presence_producer_publication_take(&receipt);'
mutation duplicate-record \
	's/record = (void \*)lb_new_record(header);/record = (void *)lb_new_record(header); (void)lb_new_record(header);/'
mutation no-composition-reentry-check \
	'/__atomic_load_n(&publication_state, __ATOMIC_ACQUIRE) !=/,+1d'
mutation published-is-mutable \
	's/state == PUBLICATION_PUBLISHED ||/false ||/'
mutation reserved-loser-does-not-poison \
	'/if (!claim(PUBLICATION_RESERVED, PUBLICATION_BUSY))/,+1s/return fail();/return CB_ERR;/'

selector_check()
{
	kconfig=$1
	! grep -Eq 'select[[:space:]]+PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION' \
		"$kconfig" && ! awk '$1 == "config" { inside = $2 == \
		"PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION"; next }
		inside && (($1 == "bool" && NF > 1) || $1 == "prompt" ||
			($1 == "default" && $2 == "y")) { bad = 1 }
		END { exit !bad }' "$kconfig"
}

if grep -R -Eq --include=Kconfig \
	'select[[:space:]]+PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION' "$root/src" || \
	! selector_check "$root/src/lib/Kconfig"; then
	echo 'presence publication became selectable or selected' >&2
	exit 1
fi

route_check()
{
	! grep -Eq 'out[bwl]|payload_mm_authvar_presence_smi_dispatch|APM_CNT' "$1"
}

if ! route_check "$source"; then
	echo 'presence publication gained an APM route' >&2
	exit 1
fi

sed '$a select PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION' \
	"$root/src/lib/Kconfig" > "$temporary/selected.Kconfig"
if selector_check "$temporary/selected.Kconfig"; then
	echo 'no-selector mutation survived' >&2
	exit 1
fi

sed '$a void mutation_route(void) { outb(0, APM_CNT); }' "$source" \
	> "$temporary/route.c"
if route_check "$temporary/route.c"; then
	echo 'no-route mutation survived' >&2
	exit 1
fi

table_source="$root/src/lib/coreboot_table.c"
bootmem=$(grep -n 'bootmem_write_memory_table(lb_memory(head))' \
	"$table_source" | cut -d: -f1)
publish=$(grep -n 'lb_add_payload_mm_authvar_presence_endpoint(head,' \
	"$table_source" | cut -d: -f1)
boot_mode=$(grep -n 'lb_add_boot_mode(head)' "$table_source" | cut -d: -f1)
finalize=$(grep -n 'return lb_table_fini(head)' "$table_source" | cut -d: -f1)
overflow=$(grep -n 'Authenticated-variable presence table overflow' \
	"$table_source" | cut -d: -f1)
if [ -z "$bootmem" ] || [ -z "$boot_mode" ] || [ -z "$publish" ] || \
	[ -z "$finalize" ] || [ "$boot_mode" -ge "$publish" ] || \
	[ "$publish" -ge "$finalize" ] || [ -z "$overflow" ]; then
	echo 'presence table order or armed overflow fail-stop was weakened' >&2
	exit 1
fi

boundary_check()
{
	implementation=$1
	[ "$(grep -c 'lb_add_boot_mode(head)' "$implementation")" -eq 1 ] ||
		return 1
	[ "$(grep -c 'lb_add_payload_mm_authvar_presence_endpoint(head,' \
		"$implementation")" -eq 1 ] || return 1
	[ "$(grep -c 'return lb_table_fini(head)' "$implementation")" -eq 1 ] ||
		return 1
	boot_mode=$(grep -n 'lb_add_boot_mode(head)' \
		"$implementation" | cut -d: -f1)
	publish=$(grep -n 'lb_add_payload_mm_authvar_presence_endpoint(head,' \
		"$implementation" | cut -d: -f1)
	finalize=$(grep -n 'return lb_table_fini(head)' \
		"$implementation" | cut -d: -f1)
	overflow=$(grep -n 'Authenticated-variable presence table overflow' \
		"$implementation" | cut -d: -f1)
	[ -n "$boot_mode" ] && [ -n "$publish" ] && [ -n "$finalize" ] && \
		[ "$boot_mode" -lt "$publish" ] && [ "$publish" -lt "$finalize" ] && \
		[ -n "$overflow" ] && \
		! sed -n "$((publish + 1)),$((finalize - 1))p" "$implementation" | \
			grep -Eq 'lb_[[:alnum:]_]+\(head'
}

sed -e '/lb_add_boot_mode(head)/d' \
	-e '/Authenticated-variable presence publication failed/a\
\tlb_add_boot_mode(head);' \
	"$table_source" > "$temporary/before-order.c"
if boundary_check "$temporary/before-order.c"; then
	echo 'table source-order mutation survived' >&2
	exit 1
fi

sed '/Authenticated-variable presence publication failed/a\
\tlb_add_boot_mode(head);' "$table_source" > "$temporary/later-builder.c"
if boundary_check "$temporary/later-builder.c"; then
	echo 'post-publication table builder mutation survived' >&2
	exit 1
fi

sed '/Authenticated-variable presence table overflow/d' "$table_source" \
	> "$temporary/no-overflow-failstop.c"
if boundary_check "$temporary/no-overflow-failstop.c"; then
	echo 'table overflow fail-stop mutation survived' >&2
	exit 1
fi

take=$(grep -n 'producer_publication_take(&receipt)' "$source" | cut -d: -f1)
record=$(grep -n 'record = (void \*)lb_new_record(header)' "$source" | cut -d: -f1)
if [ -z "$take" ] || [ -z "$record" ] || [ "$record" -ge "$take" ]; then
	echo 'presence final slot is not reserved before the one-shot take' >&2
	exit 1
fi

echo 'Payload-MM authenticated-variable presence publication tests: PASS'
