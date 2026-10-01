#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

# The composite rearm poison owns both the retained record and DMA epoch.
# Dispatcher fail-stop must call it exactly once and must not duplicate the
# lower-level epoch poison.
sed -n '/static void __noreturn s3_fail_stop(void)/,/^}/p' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c" \
	> "$temporary/s3-fail-stop.c"
test "$(grep -c 'starbook_mtl_authvar_presence_s3_rearm_poison();' \
	"$temporary/s3-fail-stop.c")" -eq 1
test "$(grep -c 'starbook_mtl_dma_smm_epoch_poison();' \
	"$temporary/s3-fail-stop.c" || true)" -eq 0
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
	'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_S3_RECORD 1' \
	'#define CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM 1' \
	'#define CONFIG_SMM_APMC_ROUTE_STARBOOK_MTL_DMA_RECEIPT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY 1' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include -I$root/src/soc/intel/common/block/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -pthread -no-pie"
sources="$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_dispatch_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c"

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
	'-O1 -g -fsanitize=thread -fno-omit-frame-pointer -Wno-tsan'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		TSAN_OPTIONS=halt_on_error=1 "$temporary/test"
done

production="$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.c"
mutant="$temporary/dispatch-mutant.c"
sed 's/SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE, \&cause/0xffU, \&cause/' \
	"$production" > "$mutant"
! cmp -s "$production" "$mutant"
# shellcheck disable=SC2086
${CC:-cc} $common -O2 \
	"$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_dispatch_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.c" \
	"$mutant" -o "$temporary/mutant"
if "$temporary/mutant" >/dev/null 2>&1; then
	echo 'lifecycle-close exact-selector mutation survived' >&2
	exit 1
fi

sed '/if (starbook_mtl_authvar_presence_lifecycle_close_install_receive(/{N;c\
\t\t\tif (((void)install_policy, false) ||
}' \
	"$production" > "$mutant"
! cmp -s "$production" "$mutant"
# shellcheck disable=SC2086
${CC:-cc} $common -O2 \
	"$root/tests/lib/starbook_mtl_authvar_presence_lifecycle_close_dispatch_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.c" \
	"$mutant" -o "$temporary/install-mutant"
if "$temporary/install-mutant" >/dev/null 2>&1; then
	echo 'lifecycle-close install receiver mutation survived' >&2
	exit 1
fi

echo 'StarBook MTL lifecycle-close pre-lock dispatch: PASS'
