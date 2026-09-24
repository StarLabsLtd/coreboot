#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mde="$root/src/vendorcode/intel/edk2/UDK2017/MdePkg/Include"
hostcc="${HOSTCC:-cc}"
machine="$(uname -m)"
case "$machine" in
x86_64) processor="$mde/X64" ;;
i?86) processor="$mde/Ia32" ;;
*) echo "Unsupported machine: $machine" >&2; exit 1 ;;
esac

compile_test()
{
	optimization="$1"
	formatter="$2"
	binary="$3"
	"$hostcc" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/vendorcode/intel/edk2" -I"$mde" -I"$processor" \
		-I"$root/util/cbfstool/flashmap" \
		"$root/tests/lib/payload_mm_authvar_fv_test.c" \
		"$root/util/smmstoretool/fv.c" "$formatter" -o "$binary"
}

for optimization in 0 2; do
	compile_test "$optimization" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$tmp/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization"
done

mutation_test()
{
	name="$1"
	expression="$2"
	killer="${3:-}"
	mutant="$tmp/payload_mm_authvar_fv-$name.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_fv.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_fv.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		log="$binary.log"
		if ! compile_test "$optimization" "$mutant" "$binary" \
			>"$log" 2>&1; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		if [ -n "$killer" ] &&
		   ASAN_OPTIONS=detect_leaks=1 "$binary" "$killer" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
		if [ -z "$killer" ] &&
		   ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutation_test odd-geometry \
	's/spare_blocks = blocks \/ 2U;/spare_blocks = (blocks + 1U) \/ 2U;/'
mutation_test geometry-span \
	's/!span_valid(geometry, sizeof(\*geometry))/!geometry/' \
	geometry-wrap
mutation_test geometry-alignment \
	's/(uintptr_t)geometry % _Alignof(\*geometry))/(false))/'
mutation_test region-span \
	's/!span_valid(fv, region_size)/!fv/' \
	region-wrap
mutation_test checksum-offset \
	's/write_le16(fv + 50U, (uint16_t)(0U - checksum));/write_le16(fv + 52U, (uint16_t)(0U - checksum));/'
mutation_test fv-signature \
	's/write_le32(fv + 40U, FV_SIGNATURE);/write_le32(fv + 40U, FV_SIGNATURE + 1U);/'
mutation_test fv-guid \
	's/0x8d, 0x2b, 0xf1, 0xff/0x8c, 0x2b, 0xf1, 0xff/'
mutation_test fv-attributes \
	's/write_le32(fv + 44U, FV_SMMSTORE_ATTRIBUTES);/write_le32(fv + 44U, FV_SMMSTORE_ATTRIBUTES + 1U);/'
mutation_test fv-header-length \
	's/write_le16(fv + 48U, PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE);/write_le16(fv + 48U, PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE - 2U);/'
mutation_test fv-revision \
	's/fv\[55\] = FV_REVISION;/fv[55] = (uint8_t)(FV_REVISION + 1U);/'
mutation_test block-map-count \
	's/write_le32(fv + 56U, geometry.block_count);/write_le32(fv + 56U, geometry.block_count - 1U);/'
mutation_test store-guid \
	's/0x78, 0x2c, 0xf3, 0xaa/0x79, 0x2c, 0xf3, 0xaa/'
mutation_test store-size \
	's/geometry.variable_size - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE);/geometry.variable_size - PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE - 1U);/'
mutation_test store-format \
	's/#define VARIABLE_STORE_FORMATTED 0x5aU/#define VARIABLE_STORE_FORMATTED 0x5bU/'
mutation_test store-state \
	's/#define VARIABLE_STORE_HEALTHY 0xfeU/#define VARIABLE_STORE_HEALTHY 0xfdU/'

printf '%s\n' 'Authenticated-variable FV formatter/geometry tests: PASS'
