#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/mtl-mor-private-boundary.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 16' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	> "$temporary/include/config.h"

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-pthread \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/tests/lib" \
		"$root/tests/lib/starbook_mtl_mor_private_boundary_test.c" \
		-o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

source="$root/src/mainboard/starlabs/starbook/variants/mtl/mor_private_boundary.c"
mutant="$temporary/mor-private-boundary-no-published-secret-scrub.c"
sed '/MTL_MOR_PRIVATE_PUBLISHING, MTL_MOR_PRIVATE_PUBLISHED/,/scrub(&candidate/ {
	/scrub(private_boundary.receipt_secret,/ {
		N
		c\
\t(void)0;
	}
}' "$source" > "$mutant"
test "$(grep -c 'scrub(private_boundary.receipt_secret,' "$source")" -eq 2
test "$(grep -c 'scrub(private_boundary.receipt_secret,' "$mutant")" -eq 1
mutant_output="$temporary/no-published-secret-scrub"
${HOSTCC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
	-Wconversion -Wshadow -Wstrict-prototypes \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
	-pthread -D__TEST__ -D__COREBOOT__ \
	"-DMOR_PRIVATE_BOUNDARY_SOURCE=\"$mutant\"" \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -I"$root/tests/lib" \
	-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
	"$root/tests/lib/starbook_mtl_mor_private_boundary_test.c" \
	-o "$mutant_output"
if ASAN_OPTIONS=detect_leaks=1 "$mutant_output" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: missing retained receipt-secret scrub survived' >&2
	exit 1
fi

verify_ramstage_only()
{
	makefile=$1
	test "$(grep -Ec '^[^#]*mor_private_boundary.c$' "$makefile")" -eq 1 &&
		test "$(grep -c '^ramstage-\$(CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) += mor_private_boundary.c$' \
			"$makefile")" -eq 1
}
makefile="$root/src/mainboard/starlabs/starbook/variants/mtl/Makefile.mk"
verify_ramstage_only "$makefile"
mutant="$temporary/Makefile-wrong-stage.mk"
cp "$makefile" "$mutant"
printf '%s\n' \
	'smm-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER) += mor_private_boundary.c' \
	>> "$mutant"
if verify_ramstage_only "$mutant"; then
	printf '%s\n' 'ERROR: wrong-stage private boundary survived' >&2
	exit 1
fi

printf '%s\n' 'StarBook MTL MOR private boundary tests: PASS'
