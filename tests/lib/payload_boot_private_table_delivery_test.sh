#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
# Actual version owner supplies ASan-registered, otherwise unused table strings.
printf '%s\n' '#define COREBOOT_VERSION "component-test"' \
	'#define COREBOOT_BUILD "component-test"' '#define COREBOOT_COMPILE_TIME "00:00:00"' \
	'#define COREBOOT_VERSION_TIMESTAMP 0' '#define COREBOOT_MAJOR_VERSION 0' \
	'#define COREBOOT_MINOR_VERSION 0' '#define COREBOOT_DMI_DATE "01/01/2000"' \
	'#define COREBOOT_BUILD_YEAR_BCD 0' '#define COREBOOT_BUILD_MONTH_BCD 1' \
	'#define COREBOOT_BUILD_DAY_BCD 1' '#define COREBOOT_BUILD_WEEKDAY_BCD 6' \
	'#define ASL_VERSION 0' > "$temporary/include/build.h"
consumer_source=${BOOT_PRIVATE_TABLE_CDK2_SOURCE:?set CDK2 source for the opposing importer}
consumer_revision=$(git -C "$consumer_source" rev-parse \
	"${BOOT_PRIVATE_TABLE_CDK2_REVISION:-HEAD}^{commit}")
consumer="$temporary/consumer-source"
mkdir -p "$consumer" "$temporary/consumer-config/cdk2"
git -C "$consumer_source" archive "$consumer_revision" include src/boot \
	src/modules/pci_host_bridge | tar -x -C "$consumer"
cp "$consumer_source/build/cdk2/include/cdk2/config.h" \
	"$temporary/consumer-config/cdk2/config.h"
printf 'Opposing CDK2 source: %s\n' "$consumer_revision"
sha256sum "$temporary/consumer-config/cdk2/config.h"
make -C "$root" build-tests/lib/bootmem-aligned-reservation-test >/dev/null
config="$root/build/tests/tests/lib/bootmem-aligned-reservation-test"
printf '#include "%s/config.h"\n' "$config" > "$temporary/include/config.h"
printf '%s\n' '#undef CONFIG_SMM_INVOCATION_RUNTIME_BINDING' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#undef CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
	'#undef CONFIG_PAYLOAD_BOOT_PRIVATE_BUFFER' '#define CONFIG_PAYLOAD_BOOT_PRIVATE_BUFFER 1' \
	'#undef CONFIG_MAX_CPUS' '#define CONFIG_MAX_CPUS 4' \
	>> "$temporary/include/config.h"
for optimization in 0 2; do
	"${CC:-cc}" -std=gnu11 -g -O"$optimization" -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -fshort-wchar \
		-ffunction-sections -fdata-sections -fno-pie -no-pie \
		-I"$temporary/consumer-config" -I"$consumer/include" -I"$consumer/src/boot" \
		-include "$temporary/consumer-config/cdk2/config.h" \
		"$root/tests/lib/payload_boot_private_table_consumer_test.c" \
		"$consumer/src/boot/coreboot.c" "$consumer/src/boot/coreboot_checksum.c" \
		"$consumer/src/boot/coreboot_resource.c" "$consumer/src/boot/coreboot_hobs.c" \
		"$consumer/src/boot/coreboot_dma_handoff.c" \
		"$consumer/src/modules/pci_host_bridge/model.c" \
		-Wl,--gc-sections -o "$temporary/consumer"
	for variant in owner-query receipt-fallback; do
		sender="$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_sender.c"
		if [ "$variant" = receipt-fallback ]; then
			# Hostile replacement: trust the echoed receipt instead of the committed owner.
			sed 's/bootmem_aligned_reservation_query(\&boot_private.handle, \&reservation)/(reservation = (struct bootmem_aligned_reservation){ .base = boot_private.base, .size = boot_private.bytes, .tag = BM_MEM_RESERVED }, 0)/' \
				"$sender" > "$temporary/receipt-fallback.c"
			sender="$temporary/receipt-fallback.c"
		fi
		"${CC:-cc}" -std=gnu23 -g -O"$optimization" -Wall -Wextra -Werror -Wundef \
			-Wno-unused-parameter -Wno-sign-compare -Wstrict-prototypes \
			-fno-builtin -fno-pie -fno-pic -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-omit-frame-pointer -ffunction-sections -fdata-sections \
			-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ \
			-DBOOTMEM_RECEIPT_TEST -include "$root/src/include/kconfig.h" \
			-include "$root/tests/lib/bootmem_reservation_receipt_config.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$config" -I"$root/tests/include/mocks" \
			-I"$root/tests/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" -I"$root/build/tests" \
			-I"$root/tests/include/tests/lib/fmap" \
			-I"$root/3rdparty/vboot/firmware/include" \
			-I"$root/3rdparty/vboot/firmware/2lib/include" \
			-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
			"$root/tests/lib/payload_boot_private_table_delivery_test.c" \
			"$root/tests/lib/payload_boot_private_table_component.c" \
			"$sender" \
			"$root/src/lib/payload_mm_authvar_presence_tuple_sender.c" \
			"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
			"$root/src/lib/payload_boot_private_buffer.c" \
			"$root/src/lib/payload_mm_authvar_service.c" \
			"$root/src/lib/bootmem.c" "$root/src/lib/bootmem_reservation_receipt.c" \
			"$root/src/lib/memrange.c" "$root/src/device/device_util.c" \
			"$root/src/lib/version.c" \
			"$root/src/commonlib/bsd/ipchksum.c" \
			-Wl,--gc-sections -no-pie -o "$temporary/test"
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" valid > "$temporary/table.bin"
		for scenario in valid checksum truncated nonreserved wrong-geometry duplicate; do
			ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
				"$temporary/consumer" "$temporary/table.bin" "$scenario"
		done
		for scenario in failed-ack mixed-revision wrong-span wrong-handle; do
			result=0
			ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
				"$temporary/test" "$scenario" > "$temporary/refused.bin" \
				2> "$temporary/refused.log" || result=$?
			[ "$result" -eq 77 ] && [ ! -s "$temporary/refused.bin" ]
			if grep -Ei 'assertion|oracle failure|runtime error:|Sanitizer' "$temporary/refused.log"; then
				exit 1
			fi
		done
		result=0
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" owner-drift > "$temporary/drift.bin" \
			2> "$temporary/drift.log" || result=$?
		if grep -Ei 'assertion|oracle failure|runtime error:|Sanitizer' "$temporary/drift.log"; then
			exit 1
		fi
		if [ "$variant" = owner-query ]; then
			[ "$result" -eq 77 ] && [ ! -s "$temporary/drift.bin" ]
		else
			[ "$result" -eq 0 ] && [ -s "$temporary/drift.bin" ]
		fi
	done
done
printf '%s\n' 'BOOT-private actual ACK/table publication tests: PASS (modeled trigger admission)'
