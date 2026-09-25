#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/mtl-mor-platform-smm.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 16' \
	'#define CONFIG_SMM_MODULE_STACK_SIZE 0x4000' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	> "$temporary/include/config.h"

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/soc/intel/common/block/include" \
		"$root/tests/lib/starbook_mtl_mor_platform_smm_test.c" -o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

verify_smm_only()
{
	makefile=$1
	test "$(grep -Ec '^[^#]*mor_platform_smm.c$' "$makefile")" -eq 1 &&
		test "$(grep -c '^smm-\$(CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) += mor_platform_smm.c$' \
			"$makefile")" -eq 1
}
makefile="$root/src/mainboard/starlabs/starbook/variants/mtl/Makefile.mk"
verify_smm_only "$makefile"
mutant="$temporary/Makefile-wrong-stage.mk"
cp "$makefile" "$mutant"
printf '%s\n' \
	'ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) += mor_platform_smm.c' \
	>> "$mutant"
if verify_smm_only "$mutant"; then
	printf '%s\n' 'ERROR: wrong-stage SMM bootstrap survived' >&2
	exit 1
fi

printf '%s\n' 'StarBook MTL MOR SMM platform tests: PASS'
