#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include/arch" "$temporary/include"
# HOST-modeled options, serialized like the unit-test configuration overrides.
while IFS='=' read -r config_key config_value; do
	printf '#define %s %s\n' "$config_key" "$config_value"
done > "$temporary/include/config.h" <<'EOF'
CONFIG_DEFAULT_CONSOLE_LOGLEVEL=0
CONFIG_FATAL_ASSERTS=0
CONFIG_ELOG=0
CONFIG_QEMU_PFLASH_VOLATILE_LEASE=1
CONFIG_ROM_SIZE=(64U * 1024U)
EOF
cat > "$temporary/include/test_base.h" <<'EOF'
#include <stdint.h>
extern uint8_t qemu_pflash_test_region[];
#define QEMU_PFLASH_BASE ((uintptr_t)qemu_pflash_test_region)
EOF
cat > "$temporary/include/arch/mmio.h" <<'EOF'
#ifndef TEST_ARCH_MMIO_H
#define TEST_ARCH_MMIO_H
#include <stdint.h>
uint8_t qemu_pflash_test_read8(const volatile void *address);
void qemu_pflash_test_write8(volatile void *address, uint8_t value);
#define read8(address) qemu_pflash_test_read8(address)
#define write8(address, value) qemu_pflash_test_write8(address, value)
#endif
EOF

compile_test()
{
	name=$1
	test_source=$2
	shift 2
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -Wshadow \
		-Wno-sign-compare -Wstrict-prototypes -fno-builtin -pthread "$@" \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-include "$temporary/include/test_base.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/tests/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$test_source" \
		"$root/src/commonlib/region.c" "$root/src/commonlib/mem_pool.c" \
		-o "$temporary/$name"
}

run_test()
{
	name=$1
	shift
	compile_test "$name" \
		"$root/tests/lib/qemu_pflash_lease_standalone_test.c" "$@"
	"$temporary/$name"
}

mutant()
{
	mutation_name=$1
	expression=$2
	mutant_source="$temporary/$mutation_name-rom_media.c"
	mutant_test="$temporary/$mutation_name-test.c"
	cp "$root/src/mainboard/emulation/qemu-i440fx/rom_media.c" "$mutant_source"
	cp "$root/tests/lib/qemu_pflash_lease_standalone_test.c" "$mutant_test"
	perl -0pi -e "$expression" "$mutant_source"
	cmp -s "$mutant_source" \
		"$root/src/mainboard/emulation/qemu-i440fx/rom_media.c" && {
		printf '%s\n' "mutant $mutation_name did not modify its source" >&2
		exit 1
	}
	perl -0pi -e 's{#include "[.][.]/[.][.]/src/mainboard/emulation/qemu-i440fx/rom_media[.]c"}{#include "'"$mutant_source"'"}' "$mutant_test"
	compile_test "mutant-$mutation_name" "$mutant_test" -O2
	if "$temporary/mutant-$mutation_name" >/dev/null 2>&1; then
		printf '%s\n' "mutant $mutation_name survived" >&2
		exit 1
	fi
	printf '%s\n' "mutant $mutation_name: rejected"
}

run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test thread-O1 -O1 -g -fno-omit-frame-pointer -fsanitize=thread

mutant permit-peer-write \
	's/!pflash_lease[.]boundary &&\n\t    pflash_lease[.]state == QEMU_PFLASH_LEASE_IDLE &&/true \&\&/'
mutant ignore-owner-mutation \
	's/!lease_handle_valid\(([^)]*)\)/false/g'
mutant ignore-callback-mutation \
	's/!lease_callbacks_valid\(\)/false/g'
mutant permit-second-owner \
	's/pflash_lease[.]state != QEMU_PFLASH_LEASE_IDLE \|\|\n\t    pflash_lease[.]boundary \|\|/false ||/'
mutant overflow-prone-span \
	's/offset <= region_device_sz\(root\) &&\n\t\tsize <= region_device_sz\(root\) - offset/offset + size <= region_device_sz(root)/'
mutant permit-reentry \
	's/pflash_lease[.]state != QEMU_PFLASH_LEASE_ACTIVE \|\|/false ||/'
mutant accept-not-ready \
	's/(static int pflash_status_restore\(size_t offset\).*?unsigned int attempt;\n\t)int result = -1;/${1}int result = 0;/s'
mutant ignore-status-errors \
	's/result = status & \(PROGRAM_ERROR_STATUS \| ERASE_ERROR_STATUS\) \?\n\t\t\t\t-1 : 0;/result = 0;/'
mutant omit-read-array-restore \
	's/write8\(address, READ_ARRAY_CMD\);/(void)address;/'

padding_mutant()
{
	name=$1
	old=$2
	new=$3
	assertion=$4
	source="$temporary/$name-rom_media.c"
	test_source="$temporary/$name-test.c"
	cp "$root/src/mainboard/emulation/qemu-i440fx/rom_media.c" "$source"
	OLD="$old" NEW="$new" perl -0pi -e '
		$count = s/\Q$ENV{OLD}\E/$ENV{NEW}/g;
		die "expected exactly one source match\n" unless $count == 1;
	' "$source"
	cp "$source" "$temporary/reversed.c"
	OLD="$new" NEW="$old" perl -0pi -e '
		$count = s/\Q$ENV{OLD}\E/$ENV{NEW}/g;
		die "expected exactly one inverse match\n" unless $count == 1;
	' "$temporary/reversed.c"
	cmp "$temporary/reversed.c" "$root/src/mainboard/emulation/qemu-i440fx/rom_media.c"
	cp "$root/tests/lib/qemu_pflash_lease_standalone_test.c" "$test_source"
	perl -0pi -e 's{#include "[.][.]/[.][.]/src/mainboard/emulation/qemu-i440fx/rom_media[.]c"}{#include "'"$source"'"}' "$test_source"
	compile_test "$name" "$test_source" -O2 -g -fno-pie -no-pie \
		-fsanitize=address,undefined -fno-sanitize-recover=all
	status=0
	ASAN_OPTIONS=detect_leaks=1 "$temporary/$name" > "$temporary/$name.log" 2>&1 || status=$?
	test "$status" = 134
	grep -F "assertion failed: $assertion" "$temporary/$name.log"
	if grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
		"$temporary/$name.log"; then
		exit 1
	fi
	printf '%s\n' "padding mutant $name: targeted assertion PASS"
}

padding_mutant ignore-erased-span 'if (erased) {' 'if (erased || !erased) {' \
	'qemu_pflash_lease_program(root, &owner, 0, erased, sizeof(erased)) == 0'
padding_mutant omit-ff-skip 'if (source[index] == 0xff)' 'if (false)' \
	'program_count == before'

printf '%s\n' 'QEMU pflash owner-lease tests: PASS'
