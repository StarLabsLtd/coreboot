#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY 1' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-ffunction-sections -fdata-sections -Wl,--gc-sections
	-D__COREBOOT__ -D__TEST__ -DBOOTMEM_RECEIPT_TEST
	-include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include -I$root/src/lib
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_lifecycle_mailbox_authority_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_mailbox.c"

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags -Wno-conversion -c \
		"$root/src/lib/bootmem_reservation_receipt.c" \
		-o "$temporary/receipt.o"
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags -Wno-conversion -c \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c" \
		-o "$temporary/policy.o"
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources "$temporary/receipt.o" \
		"$temporary/policy.o" \
		-o "$temporary/test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$temporary/test"
done

# shellcheck disable=SC2086
${CC:-cc} $common -m32 -Wno-conversion -c \
	"$root/tests/lib/starbook_mtl_lifecycle_mailbox_authority_test.c" \
	-o "$temporary/test32.o"
# shellcheck disable=SC2086
${CC:-cc} $common -m32 -Wno-conversion -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_mailbox.c" \
	-o "$temporary/mailbox32.o"

echo 'StarBook MTL lifecycle mailbox authority: PASS'
