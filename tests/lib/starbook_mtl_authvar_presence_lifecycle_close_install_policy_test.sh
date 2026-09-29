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
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_VIEW 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT 1' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__SMM__
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-isystem $root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include -I$root/src/soc/intel/common/block/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl"
source="$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_policy.c"
test_source="$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_install_policy_test.c"

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
	'-O1 -g -fsanitize=thread -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags -c "$source" -o "$temporary/policy.o"
done

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
	'-O1 -g -fsanitize=thread -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags -pthread "$test_source" -o "$temporary/policy-test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		TSAN_OPTIONS=halt_on_error=1 "$temporary/policy-test"
done

# Treat commonlib headers as system headers so their legacy inline conversions
# do not mask strict conversion checking of this policy translation unit.
# shellcheck disable=SC2086
${CC:-cc} $common -O2 -m32 -fstack-usage -c "$source" \
	-o "$temporary/policy-ia32.o"
awk -F '\t' '$2 > 1024 { exit 1 }' "$temporary/policy-ia32.su"

echo 'StarBook MTL lifecycle-close install policy compile/behavior tests: PASS'
