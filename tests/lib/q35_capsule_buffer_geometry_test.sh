#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
test "$#" -eq 2
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
selected=$1
disabled=$2
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
grep -qx 'CONFIG_Q35_SMM_CAPSULE_BROKER_BUFFERS=y' "$selected/auto.conf"
grep -qx 'CONFIG_CAPSULE_BROKER_CBMEM_BUFFERS=y' "$selected/auto.conf"
if grep -q '^CONFIG_Q35_SMM_CAPSULE_BROKER_BUFFERS=y$' "$disabled/auto.conf"; then
	exit 1
fi
rom_size=$(sed -n 's/^CONFIG_ROM_SIZE=\(0x[0-9a-fA-F]*\)$/\1/p' "$selected/auto.conf")
test -n "$rom_size"
test "$(wc -c < "$selected/coreboot.rom")" -eq "$((rom_size))"
staging_size=$(((rom_size + 1048576 + 4095) & ~4095))

compile_observer()
{
	"${HOSTCC:-cc}" -m32 -Os -Wall -Wextra -Werror -Wstrict-prototypes \
		-ffreestanding -ffunction-sections -fno-stack-protector \
		-fno-pie -fno-builtin -nostdlib -no-pie \
		-Wl,--gc-sections -Wl,-e,_start \
		-DEXPECTED_STAGING_SIZE="$2" -DEXPECTED_TOP_RESERVATION_SIZE="$3" \
		"$root/tests/lib/q35_capsule_buffer_geometry_test.c" \
		"$1/romstage/mainboard/emulation/qemu-q35/memmap.o" -o "$4"
}

compile_observer "$selected" "$staging_size" 0 "$temporary/selected"
"$temporary/selected"
printf 'Q35 actual IA32 selected staging=%s/top=0: PASS\n' "$staging_size"
compile_observer "$disabled" 0 0 "$temporary/disabled"
"$temporary/disabled"
printf '%s\n' 'Q35 actual IA32 disabled staging=0/top=0: PASS'
compile_observer "$selected" 0 0 "$temporary/wrong-staging"
set +e
"$temporary/wrong-staging"
status=$?
set -e
test "$status" -eq 1
printf '%s\n' 'Q35 actual IA32 wrong-staging oracle: PASS'
compile_observer "$selected" "$staging_size" 4096 "$temporary/wrong-adjacency"
set +e
"$temporary/wrong-adjacency"
status=$?
set -e
test "$status" -eq 2
printf '%s\n' 'Q35 actual IA32 wrong-adjacency oracle: PASS'
