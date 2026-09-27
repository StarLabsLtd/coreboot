#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include/intelblocks"

cat > "$temporary/include/cf9_reset.h" <<'EOF'
void do_full_reset(void);
void do_system_reset(void);
EOF
cat > "$temporary/include/halt.h" <<'EOF'
#include <commonlib/bsd/compiler.h>
void halt(void) __noreturn;
EOF
cat > "$temporary/include/intelblocks/tco.h" <<'EOF'
#include <stdint.h>
uint16_t tco_read_reg(uint16_t reg);
void tco_write_reg(uint16_t reg, uint16_t value);
uint32_t tco_get_timer_min_value(void);
EOF

cflags="-std=gnu11 -Wall -Wextra -Werror -D__COREBOOT__"
includes="-I$temporary/include -I$root/src/include -I$root/src \
-I$root/src/arch/x86/include -I$root/src/commonlib/include \
-I$root/src/commonlib/bsd/include"
q35_source="$root/src/mainboard/emulation/qemu-q35/smm_invocation_fail_stop.c"
mtl_source="$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_fail_stop.c"

if rg -q '__weak|callback|context|void[[:space:]]*\*|\(\*' \
	"$q35_source" "$mtl_source"; then
	printf '%s\n' 'fail-stop provider gained weak or transported state' >&2
	exit 1
fi
grep -q '^smm-$(CONFIG_Q35_SMM_INVOCATION_FAIL_STOP_TEST) += smm_invocation_fail_stop.c$' \
	"$root/src/mainboard/emulation/qemu-q35/Makefile.mk"
grep -q '^smm-$(CONFIG_STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP) += smm_invocation_fail_stop.c$' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/Makefile.mk"

# Keep the production symbols strong for the exact link-cardinality checks.
${CC:-cc} $cflags $includes -c "$q35_source" -o "$temporary/q35.o"
${CC:-cc} $cflags $includes -c "$mtl_source" -o "$temporary/mtl.o"
printf '%s\n' \
	'#include <cpu/x86/smm_invocation_fail_stop.h>' \
	'int main(void) { smm_invocation_platform_fail_stop(); }' \
	> "$temporary/consumer.c"
${CC:-cc} $cflags $includes -c "$temporary/consumer.c" -o "$temporary/consumer.o"

if ${CC:-cc} "$temporary/consumer.o" -o "$temporary/missing" \
	>"$temporary/missing.log" 2>&1; then
	printf '%s\n' 'consumer linked without a fail-stop provider' >&2
	exit 1
fi
grep -q 'smm_invocation_platform_fail_stop' "$temporary/missing.log"

printf '%s\n' \
	'#include <stdbool.h>' \
	'#include <stdint.h>' \
	'void do_full_reset(void) {}' \
	'void do_system_reset(void) {}' \
	'uint32_t tco_get_timer_min_value(void) { return 2; }' \
	'uint16_t tco_read_reg(uint16_t reg) { (void)reg; return 0; }' \
	'void tco_write_reg(uint16_t reg, uint16_t value) { (void)reg; (void)value; }' \
	'void halt(void) { __builtin_trap(); }' > "$temporary/link_stubs.c"
${CC:-cc} $cflags $includes -c "$temporary/link_stubs.c" \
	-o "$temporary/link_stubs.o"
${CC:-cc} "$temporary/consumer.o" "$temporary/q35.o" \
	"$temporary/link_stubs.o" -o "$temporary/one-provider"
if ${CC:-cc} "$temporary/consumer.o" "$temporary/q35.o" \
	"$temporary/mtl.o" "$temporary/link_stubs.o" \
	-o "$temporary/duplicate" >"$temporary/duplicate.log" 2>&1; then
	printf '%s\n' 'two fail-stop providers linked together' >&2
	exit 1
fi
grep -q 'multiple definition.*smm_invocation_platform_fail_stop' \
	"$temporary/duplicate.log"

# Rename only in the test objects so both real implementations can be driven.
${CC:-cc} $cflags $includes \
	-Dsmm_invocation_platform_fail_stop=q35_fail_stop \
	-c "$q35_source" -o "$temporary/q35-test.o"
${CC:-cc} $cflags $includes \
	-Dsmm_invocation_platform_fail_stop=starbook_mtl_fail_stop \
	-c "$mtl_source" -o "$temporary/mtl-test.o"
${CC:-cc} $cflags $includes \
	"$root/tests/cpu/x86/smm_invocation_fail_stop_providers_test.c" \
	"$temporary/q35-test.o" "$temporary/mtl-test.o" \
	-o "$temporary/provider-test"
"$temporary/provider-test"

# The board choice makes the two hidden, default-off providers mutually exclusive.
grep -q '^config Q35_SMM_INVOCATION_FAIL_STOP_TEST$' \
	"$root/src/mainboard/emulation/qemu-q35/Kconfig"
grep -q '^config STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP$' \
	"$root/src/mainboard/starlabs/starbook/Kconfig"
for config in Q35_SMM_INVOCATION_FAIL_STOP_TEST \
	STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP; do
	count=$(rg -l "^config $config$" "$root/src/mainboard" | wc -l)
	[ "$count" -eq 1 ]
done

# Mutants prove both fallback order and watchdog arming are observable.
sed 's/do_system_reset();/\/\* removed fallback \*\//' "$q35_source" \
	> "$temporary/q35-mutant.c"
${CC:-cc} $cflags $includes \
	-Dsmm_invocation_platform_fail_stop=q35_fail_stop \
	-c "$temporary/q35-mutant.c" -o "$temporary/q35-mutant.o"
if ${CC:-cc} $cflags $includes \
	"$root/tests/cpu/x86/smm_invocation_fail_stop_providers_test.c" \
	"$temporary/q35-mutant.o" "$temporary/mtl-test.o" \
	-o "$temporary/q35-mutant-test" && "$temporary/q35-mutant-test" \
	>/dev/null 2>&1; then
	printf '%s\n' 'Q35 missing-fallback mutant survived' >&2
	exit 1
fi
sed 's/arm_tco_watchdog();/if (0) arm_tco_watchdog();/' "$mtl_source" \
	> "$temporary/mtl-mutant.c"
${CC:-cc} $cflags $includes \
	-Dsmm_invocation_platform_fail_stop=starbook_mtl_fail_stop \
	-c "$temporary/mtl-mutant.c" -o "$temporary/mtl-mutant.o"
if ${CC:-cc} $cflags $includes \
	"$root/tests/cpu/x86/smm_invocation_fail_stop_providers_test.c" \
	"$temporary/q35-test.o" "$temporary/mtl-mutant.o" \
	-o "$temporary/mtl-mutant-test" && "$temporary/mtl-mutant-test" \
	>/dev/null 2>&1; then
	printf '%s\n' 'MTL missing-watchdog mutant survived' >&2
	exit 1
fi

printf '%s\n' 'SMM invocation fail-stop providers: PASS'
