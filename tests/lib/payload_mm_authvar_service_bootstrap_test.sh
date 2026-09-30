#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 4' '#define CONFIG_SMMSTORE 0' \
	'#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS 0' \
	'#define CONFIG_SMMSTORE_BLOCK_SIZE 65536' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 0' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	> "$temporary/include/config.h"
for optimization in 0 2; do
	# The existing receipt owner has its own strict suite but not -Wconversion.
	${CC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer -fno-pie -DBOOTMEM_RECEIPT_TEST \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-c "$root/src/lib/bootmem_reservation_receipt.c" -o "$temporary/receipt.o"
	${CC:-cc} -std=gnu11 -O"$optimization" -g -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes -fno-builtin -fno-pie -no-pie \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer -DBOOTMEM_RECEIPT_TEST \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/lib" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_service_bootstrap_test.c" \
		"$root/src/lib/payload_mm_authvar_smm_bootstrap.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$temporary/receipt.o" -o "$temporary/test"
	for mode in success with-mor repeat alias absent-identity mac tag size \
		generation unaligned high smram drift abort-active abort-empty \
		abort-prepared spi-denied absent-wave; do
		ASAN_OPTIONS=detect_leaks=0 "$temporary/test" "$mode"
	done
done
echo 'Canonical service preparation and sole bootstrap: PASS (host boundaries)'
