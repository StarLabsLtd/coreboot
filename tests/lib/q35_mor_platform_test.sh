#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/q35-mor-platform.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_MAX_CPUS 4' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	> "$temporary/include/config.h"

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-pthread -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/tests/lib" \
		"$root/tests/lib/q35_mor_platform_test.c" -o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

tsan="$temporary/test-tsan"
${HOSTCC:-cc} -std=gnu11 -O2 -Wall -Wextra -Werror \
	-Wconversion -Wshadow -Wstrict-prototypes -fno-builtin -pthread \
	-fsanitize=thread -D__TEST__ -D__COREBOOT__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" -I"$root/tests/lib" \
	"$root/tests/lib/q35_mor_platform_test.c" -o "$tsan"
if ! "$tsan" 2>"$temporary/tsan.err"; then
	if grep -q 'unexpected memory mapping' "$temporary/tsan.err"; then
		printf '%s\n' 'Q35 MOR platform TSan: unavailable on this host'
	else
		cat "$temporary/tsan.err" >&2
		exit 1
	fi
fi

# The request must be published before device initialization/SMM loading only
# for explicitly selected cold-boot providers. Preserve the normal late path.
grep -q '^config PAYLOAD_MM_AUTHVAR_MOR_EARLY_DISCOVERY$' "$root/src/lib/Kconfig"
grep -q 'CONFIG(PAYLOAD_MM_AUTHVAR_MOR_EARLY_DISCOVERY)' \
	"$root/src/lib/payload_mm_authvar_mor_linear.c"
grep -q 'BS_PRE_DEVICE, BS_ON_EXIT' "$root/src/lib/payload_mm_authvar_mor_linear.c"
grep -q 'BS_OS_RESUME_CHECK, BS_ON_ENTRY' \
	"$root/src/lib/payload_mm_authvar_mor_linear.c"
provider_block="$temporary/q35-provider-kconfig"
sed -n '/^config Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER$/,/^[^[:space:]]/p' \
	"$root/src/mainboard/emulation/qemu-q35/Kconfig" > "$provider_block"
grep -q 'depends on PAYLOAD_MM_AUTHVAR_MOR_EARLY_DISCOVERY' "$provider_block"
grep -q 'depends on !HAVE_ACPI_RESUME' "$provider_block"

printf '%s\n' 'Q35 MOR platform provider tests: PASS'
