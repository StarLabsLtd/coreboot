#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
cleanup()
{
	if [ "${KEEP_BOOT_PRIVATE_BUFFER_TMP:-0}" = 1 ]; then
		printf 'BOOT-private test artifacts: %s\n' "$temporary" >&2
	else
		rm -rf "$temporary"
	fi
}
trap cleanup EXIT HUP INT TERM
config="$root/build/tests/tests/lib/bootmem-aligned-reservation-test"
make -C "$root" build-tests/lib/bootmem-aligned-reservation-test >/dev/null

compile()
{
	optimization=$1
	source=$2
	"${CC:-cc}" -std=gnu23 -g -O"$optimization" -Wall -Wextra -Werror -Wundef \
		-Wno-unused-parameter -Wno-sign-compare -Wstrict-prototypes \
		-fno-builtin -fno-pie -fno-pic -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-omit-frame-pointer \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ \
		-DBOOTMEM_RECEIPT_TEST \
		-include "$root/src/include/kconfig.h" \
		-include "$root/tests/lib/bootmem_reservation_receipt_config.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$config" -I"$root/tests/include/mocks" -I"$root/tests/include" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$root/build/tests" \
		"$root/tests/lib/payload_boot_private_buffer_test.c" \
		"$source" \
		"$root/src/lib/bootmem.c" "$root/src/lib/bootmem_reservation_receipt.c" \
		"$root/src/lib/memrange.c" "$root/src/device/device_util.c" \
		-no-pie -o "$temporary/test"
}

run_cases()
{
	for scenario in success invalid-arguments before-map wrong-shape wrong-tag \
		signed-wrong-shape signed-wrong-tag; do
		ASAN_OPTIONS=detect_leaks=0 "$temporary/test" "$scenario" || return 1
	done
	byte=0
	while [ "$byte" -lt 96 ]; do
		ASAN_OPTIONS=detect_leaks=0 "$temporary/test" "mutation-$byte" || return 1
		byte=$((byte + 1))
	done
}

source="$root/src/lib/payload_boot_private_buffer.c"
for optimization in 0 2; do
	compile "$optimization" "$source"
	run_cases
done
for mutation in size tag verifier; do
	case "$mutation" in
	size) expression='s/bytes == LB_PAYLOAD_BOOT_PRIVATE_BUFFER_BYTES/true/' ;;
	tag) expression='s/tag == BM_MEM_RESERVED/tag == BM_MEM_TABLE/' ;;
	verifier) expression='/verification = bootmem_reservation_receipt_verify_consume_exact_tag/,/\&captured, BM_MEM_RESERVED);/c\
\tverification = CB_SUCCESS;' ;;
	esac
	sed "$expression" "$source" > "$temporary/$mutation.c"
	! cmp -s "$source" "$temporary/$mutation.c"
	compile 2 "$temporary/$mutation.c"
	if (run_cases) > "$temporary/$mutation.log" 2>&1; then
		printf 'BOOT-private %s mutant survived\n' "$mutation" >&2
		exit 1
	fi
done
printf '%s\n' 'BOOT-private real bootmem reservation/receipt tests: PASS (allocation metadata only)'
