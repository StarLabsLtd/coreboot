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
	composer="$2"
	binary="$3"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$root/src" \
		"$root/tests/lib/payload_mm_authvar_default_store_test.c" \
		"$composer" "$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_record.c" -o "$binary"
}

compile_and_run()
{
	compile_binary "$1" "$2" "$3"
	binary="$3"
	ASAN_OPTIONS=detect_leaks=1 "$binary"
}

for optimization in 0 2; do
	compile_and_run "$optimization" \
		"$root/src/lib/payload_mm_authvar_default_store.c" \
		"$tmp/test-O$optimization"
done

mutation_test()
{
	name="$1"
	expression="$2"
	mutant="$tmp/default-store-$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_default_store.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_default_store.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		log="$tmp/mutant-$name-O$optimization.log"
		if ! compile_binary "$optimization" "$mutant" "$binary" >"$log" 2>&1; then
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

mutation_test nor-direction \
	's/(source_bytes\[i\] & candidate_bytes\[i\]) == candidate_bytes\[i\]/(source_bytes[i] \& candidate_bytes[i]) == source_bytes[i]/'
mutation_test erase-precedence 's/if (erased)/if (false)/'
mutation_test complete-precedence 's/if (complete)/if (false)/'
mutation_test accept-foreign \
	's/return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_FOREIGN;/return PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET;/'
mutation_test ignore-overlap \
	's/source_address - candidate_address) < region_size)/source_address - candidate_address) >= region_size)/'
mutation_test ignore-source-range \
	's/!span_valid(source, region_size) ||/false ||/'
mutation_test omit-vendor \
	's/i < ARRAY_SIZE(defaults); i++)/i < ARRAY_SIZE(defaults) - 1U; i++)/'
mutation_test record-state \
	's/PAYLOAD_MM_AUTHVAR_STATE_ADDED/PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY/'
mutation_test custom-guid 's/0x0c, 0xec, 0x76, 0xc0/0x0d, 0xec, 0x76, 0xc0/'
mutation_test cert-attribute \
	'0,/PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |/s//PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |/'
mutation_test vendor-value \
	's/static const uint8_t vendor_keys_valid = 1U;/static const uint8_t vendor_keys_valid = 0U;/'
mutation_test timestamp-mode \
	'0,/PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO/s//PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED/'
mutation_test exact-capacity \
	's/candidate_used > geometry.variable_size/candidate_used >= geometry.variable_size/'

printf '%s\n' 'Authenticated-variable default-store tests: PASS'
