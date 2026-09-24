#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

compile()
{
	optimization="$1"
	source="$2"
	output="$3"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_certdb_test.c" "$source" \
		-o "$output"
}

run_test()
{
	ASAN_OPTIONS=detect_leaks=1 "$1"
}

for optimization in 0 2; do
	binary="$tmp/test-O$optimization"
	compile "$optimization" "$root/src/lib/payload_mm_authvar_certdb.c" "$binary"
	run_test "$binary"
done

mutate()
{
	name="$1"
	expression="$2"
	mutant="$tmp/$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_certdb.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_certdb.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/$name-O$optimization"
		log="$tmp/$name-O$optimization.log"
		if ! compile "$optimization" "$mutant" "$binary" >"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		if run_test "$binary" >"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutate total-size \
	's/read_le32(source) != source_size/(false \&\& read_le32(source) != source_size)/'
mutate node-header-bound \
	's/if (source_size - offset < CERTDB_NODE_HEADER_SIZE)/if (false \&\& source_size - offset < CERTDB_NODE_HEADER_SIZE)/'
mutate node-end-bound \
	's/node_size > source_size - offset/(false \&\& node_size > source_size - offset)/'
mutate node-size-equation \
	's/node_size != expected_size/(false \&\& node_size != expected_size)/'
mutate empty-name \
	's/if (!size || size % sizeof(uint16_t)/if ((false \&\& !size) || size % sizeof(uint16_t)/'
mutate empty-binding \
	's/if (!binding_size ||/if ((false \&\& !binding_size) ||/'
mutate embedded-name-nul \
	'/static bool name_valid/,/^}/ s/if (!bytes\[offset\] \&\& !bytes\[offset + 1U\])/if (false \&\& !bytes[offset] \&\& !bytes[offset + 1U])/'
mutate ignore-name \
	's/!memcmp(packed_name, key_name, name_size)/(key_name != NULL || true)/'
mutate ignore-guid \
	's/!memcmp(node, vendor_guid, 16U)/(vendor_guid != NULL || true)/'
mutate early-match-publication \
	's/offset += node_size;/offset += node_size; if (found.count) break;/'
mutate duplicate-match \
	's/if (found.count > 1U)/if (false \&\& found.count > 1U)/'
mutate accept-output-overlap \
	's/if (ranges_overlap(data\[left\], sizes\[left\], data\[right\], sizes\[right\]))/if (false \&\& ranges_overlap(data[left], sizes[left], data[right], sizes[right]))/'
mutate add-capacity \
	'0,/final_size > output_capacity/s//(false \&\& final_size > output_capacity)/'
mutate remove-capacity \
	'0,/final_size > output_capacity/! { /final_size > output_capacity/s//(false \&\& final_size > output_capacity)/; }'
mutate add-total \
	'0,/write_le32(bytes, (uint32_t)final_size);/s//write_le32(bytes, (uint32_t)source_size);/'
mutate remove-total \
	'0,/write_le32(bytes, (uint32_t)final_size);/! { /write_le32(bytes, (uint32_t)final_size);/s//write_le32(bytes, (uint32_t)source_size);/; }'
mutate add-offset \
	's/memcpy(bytes + source_size, vendor_guid, 16U);/memcpy(bytes + source_size + 1U, vendor_guid, 16U);/'
mutate remove-suffix \
	's/match.node_offset + match.node_size,/match.node_offset + match.node_size + 1U,/'
mutate add-u32-total \
	's/node_size > UINT32_MAX || final_size > UINT32_MAX/(false \&\& node_size > UINT32_MAX) || (false \&\& final_size > UINT32_MAX)/'
mutate add-name-count \
	's/(uint32_t)(name_size \/ sizeof(uint16_t))/(uint32_t)name_size/'
mutate add-binding-size \
	's/write_le32(bytes + source_size + 24U, (uint32_t)binding_size);/write_le32(bytes + source_size + 24U, (uint32_t)name_size);/'
mutate premature-output-bytes \
	'/payload_mm_authvar_certdb_compose/,/result = scan_certdb/ s/result = scan_certdb/memset(bytes, 0, output_capacity); result = scan_certdb/'
mutate premature-output-size \
	'0,/return PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE;/s//{ *output_size = 0U; return PAYLOAD_MM_AUTHVAR_CERTDB_NO_SPACE; }/'

printf '%s\n' 'Payload-MM authenticated-variable certdb tests: PASS'
