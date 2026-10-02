#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1
ulimit -c 0

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"
policy_source="$root/src/drivers/efi/capsule_delivery_policy.c"

compile()
{
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin \
		-D__RAMSTAGE__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" "$@" \
		"$root/tests/lib/capsule_delivery_policy_test.c" \
		"$policy_source" \
		-o "$temporary/policy"
}

run()
{
	compile "$@"
	"$temporary/policy"
}

for optimization in -O0 -O2; do
	for sanitizer in none address; do
		if test "$sanitizer" = address; then
			set -- -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie
		else
			set --
		fi
		# A compiled RAM parser alone must not advertise persistence.
		run "$optimization" "$@" -DCONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=1 \
			-DCONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=0 \
			-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE=0x123400 \
			-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE=0x567800 \
			-DTEST_EXPECTED_TRANSPORTS=0
		run "$optimization" "$@" -DCONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=1 \
			-DCONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=1 \
			-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE=0x123400 \
			-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE=0x567800 \
			-DTEST_RAM_PERSISTENCE_PROVIDER=1 -DTEST_EXPECTED_TRANSPORTS=3
		for ram in 0 1; do
			for disk in 0 1; do
				for persistence in 0 1; do
					expected=$((ram * persistence + disk * 2))
					run "$optimization" "$@" \
						-DCONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF="$ram" \
						-DCONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT="$disk" \
						-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE=0xffffffff \
						-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE=0 \
						-DTEST_RAM_PERSISTENCE_PROVIDER="$persistence" \
						-DTEST_EXPECTED_TRANSPORTS="$expected"
				done
			done
		done
	done
done

# Invalid configured limits must be compiler failures, not silently truncated wire values.
for limits in '0 1' '-1 1' '0x100000000 1' '1 -1' '1 0x100000000'; do
	set -- $limits
	if compile -O2 -DCONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=0 \
		-DCONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=1 \
		-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE="$1" \
		-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE="$2" \
		-DTEST_EXPECTED_TRANSPORTS=2 > "$temporary/invalid.log" 2>&1; then
		printf '%s\n' 'invalid capsule delivery limit compiled' >&2
		exit 1
	fi
	grep -q 'static assertion failed' "$temporary/invalid.log"
done

# Removing the production persistence guard must trip the fatal wire oracle.
guard='platform_capsule_ram_persistent(header, (uintptr_t)record)'
test "$(grep -Fc "$guard" "$policy_source")" -eq 1
sed 's/platform_capsule_ram_persistent(header, (uintptr_t)record)/1/' \
	"$policy_source" > "$temporary/persistence-guard-discard.c"
policy_source="$temporary/persistence-guard-discard.c"
for optimization in -O0 -O2; do
	compile "$optimization" -fsanitize=address,undefined -fno-omit-frame-pointer \
		-fno-pie -no-pie -DCONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF=1 \
		-DCONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT=0 \
		-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_NONPOPULATE_SIZE=0x123400 \
		-DCONFIG_DRIVERS_EFI_CAPSULE_MAX_POPULATE_SIZE=0x567800 \
		-DTEST_EXPECTED_TRANSPORTS=0
	if "$temporary/policy" > "$temporary/mutant.log" 2>&1; then
		printf '%s\n' 'persistence guard discard escaped wire oracle' >&2
		exit 1
	else
		status=$?
	fi
	test "$status" -eq 134
	if grep -Eq 'AddressSanitizer|runtime error:|UndefinedBehaviorSanitizer' \
		"$temporary/mutant.log"; then
		printf '%s\n' 'persistence mutant failed for sanitizer reason' >&2
		exit 1
	fi
done

printf '%s\n' 'capsule delivery policy producer tests PASS'
