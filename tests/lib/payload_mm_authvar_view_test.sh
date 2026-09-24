#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

compile_binary()
{
	optimization="$1"
	view_source="$2"
	binary="$3"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_view_test.c" \
		"$view_source" "$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" -o "$binary"
}

for optimization in 0 2; do
	compile_binary "$optimization" "$root/src/lib/payload_mm_authvar_view.c" \
		"$tmp/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization"
done

mutant()
{
	name="$1"
	expression="$2"
	source="$tmp/view-$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_view.c" > "$source"
	if cmp -s "$source" "$root/src/lib/payload_mm_authvar_view.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		log="$tmp/mutant-$name-O$optimization.log"
		if ! compile_binary "$optimization" "$source" "$binary" >"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant setup-bit 's/PAYLOAD_MM_AUTHVAR_MODE_SETUP }/PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT }/'
mutant secure-bit '0,/PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT }/s//PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS }/'
mutant vendor-bit 's/PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS }/PAYLOAD_MM_AUTHVAR_MODE_SETUP }/'
mutant signature-byte 's/0x12, 0xa5, 0x6c, 0x82/0x13, 0xa5, 0x6c, 0x82/'
mutant certdb-size 's/static const uint8_t cert_db_empty\[\] = { 4,/static const uint8_t cert_db_empty[] = { 5,/'
mutant synthetic-order 's/{ global_guid, setup_mode_name/{ global_guid, secure_boot_name/'
mutant certdb-attributes '0,/PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED/s//PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE/'
mutant allow-collision 's/if (payload_mm_authvar_store_find(view->persistent,/if (false \&\& payload_mm_authvar_store_find(view->persistent,/'
mutant persistent-first 's/size_t next = name_size ? (size_t)key + 1U : 0U;/size_t next = name_size ? (size_t)key + 1U : ARRAY_SIZE(synthetic);/'
mutant count-query 's/view->at_runtime, result);/!view->at_runtime, result);/'
mutant allow-unknown-mode \
	's/view->volatile_modes \& ~/\(false \&\& (view->volatile_modes \& ~/; s/PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) ||/PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS))) ||/'
mutant drop-reserved 's/synthetic_key(vendor_guid, name, name_size) >= 0/false/'

printf '%s\n' 'Authenticated-variable synthetic-view tests: PASS'
