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
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_LIFECYCLE_INSTALL_CARRIER 1' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__RAMSTAGE__
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -no-pie"
sources="$root/tests/lib/starbook_mtl_lifecycle_install_carrier_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_sender.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c"

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	for case_name in valid before-dma concurrent dma-failure stale-tail \
		alias-output; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/test" "$case_name"
	done
	if ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/test" tail-mutation >/dev/null 2>&1; then
		echo 'lifecycle carrier tail mutation survived' >&2
		exit 1
	fi
done

# shellcheck disable=SC2086
${CC:-cc} $common -m32 -O2 -Wno-conversion -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_sender.c" \
	-o "$temporary/sender32.o"
# shellcheck disable=SC2086
${CC:-cc} $common -m32 -O2 -Wno-conversion -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.c" \
	-o "$temporary/lifecycle32.o"

smm_sources="$root/tests/lib/starbook_mtl_lifecycle_install_carrier_smm_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_carrier.c"
for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $smm_sources -o "$temporary/smm-test"
	for case_name in valid wrong-base wrong-size frame-dma-overlap \
		frame-mirror-overlap dma-mirror-overlap frame-smram-overlap \
		frame-save-state-overlap frame-protected context-unprotected \
		memory-unprotected \
		memory-mutation frame-getter-mutation binding-failure \
		bad-memory-revision bad-memory-size misaligned-frame frame-above-4g \
		s3-lifecycle receipt-lifecycle-drift; do
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/smm-test" "$case_name"
	done
done

# shellcheck disable=SC2086
${CC:-cc} $common -m32 -O2 -Wno-conversion -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_carrier.c" \
	-o "$temporary/carrier-smm32.o"

echo 'StarBook MTL lifecycle install carrier: PASS'
