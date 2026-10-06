#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
cleanup()
{
	if [ "${KEEP_BOOT_PRIVATE_DELIVERY_TMP:-0}" = 1 ]; then
		printf 'BOOT-private loader delivery artifacts: %s\n' "$temporary" >&2
	else
		rm -rf "$temporary"
	fi
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$temporary/include"
make -C "$root" build-tests/lib/bootmem-aligned-reservation-test >/dev/null
config="$root/build/tests/tests/lib/bootmem-aligned-reservation-test"
# HOST override of the generated base profile; preserve its include ordering.
{
	printf '%s\n\n' '/* SPDX-License-Identifier: GPL-2.0-only */'
	while IFS='=' read -r config_key config_value; do
		printf '#undef %s\n#define %s %s\n' \
			"$config_key" "$config_key" "$config_value"
	done <<'EOF'
CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT=1
EOF
} > "$temporary/receipt-config.h"
printf '#include "%s/config.h"\n' "$config" > "$temporary/include/config.h"
printf '%s\n' '#undef CONFIG_SMM_INVOCATION_RUNTIME_BINDING' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#undef CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
	'#undef CONFIG_MAX_CPUS' '#define CONFIG_MAX_CPUS 64' \
	>> "$temporary/include/config.h"
for private in 0 1; do
	printf '#define CONFIG_PAYLOAD_BOOT_PRIVATE_BUFFER %s\n' "$private" \
		> "$temporary/include/private.h"
	for optimization in 0 2; do
		"${CC:-cc}" -std=gnu23 -g -O"$optimization" -Wall -Wextra -Werror -Wundef \
			-Wno-unused-parameter -Wno-sign-compare -Wstrict-prototypes \
			-fno-builtin -fno-pie -fno-pic -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-omit-frame-pointer \
			-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ \
			-DBOOTMEM_RECEIPT_TEST -include "$temporary/include/private.h" \
			-include "$root/src/include/kconfig.h" \
			-include "$temporary/receipt-config.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$config" -I"$root/tests/include/mocks" \
			-I"$root/tests/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" -I"$root/build/tests" \
			"$root/tests/lib/payload_boot_private_loader_delivery_test.c" \
			"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c" \
			"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
			"$root/src/lib/payload_boot_private_buffer.c" \
			"$root/src/lib/bootmem.c" "$root/src/lib/bootmem_reservation_receipt.c" \
			"$root/src/lib/memrange.c" "$root/src/device/device_util.c" \
			-no-pie -o "$temporary/test"
		for scenario in valid alias tampered wrong-span wrong-revision s3 entropy-abort; do
			ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
				"$temporary/test" "$scenario"
		done
	done
done
printf '%s\n' 'BOOT-private actual loader/receipt delivery tests: PASS (modeled seed/placement)'
