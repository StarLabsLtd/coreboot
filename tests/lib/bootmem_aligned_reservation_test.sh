#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
config="$root/build/tests/tests/lib/bootmem-aligned-reservation-test"

make -C "$root" build-tests/lib/bootmem-aligned-reservation-test >/dev/null

build_and_run()
{
	name=$1
	source=$2
	shift 2
	"${CC:-cc}" -std=gnu23 -Wall -Wextra -Werror -Wundef \
		-Wno-unused-parameter -Wno-sign-compare \
		-Wstrict-prototypes -fno-builtin -fno-pie -fno-pic "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$config" -I"$root/tests/include/mocks" -I"$root/tests/include" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$root/build/tests" \
		"$root/tests/lib/bootmem_aligned_reservation_test.c" "$source" \
		"$root/src/lib/memrange.c" "$root/src/device/device_util.c" \
		-no-pie -o "$temporary/$name"
	for case_name in success bounded atomic-capacity; do
		if ! ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
			UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/$name" "$case_name"; then
			return 1
		fi
	done
	if ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
		"$temporary/$name" capacity >/dev/null 2>&1; then
		echo "ERROR: capacity failure was published" >&2
		return 1
	fi
}

source_file="$root/src/lib/bootmem.c"
build_and_run o0 "$source_file" -O0
build_and_run o2 "$source_file" -O2
build_and_run asan "$source_file" -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan "$source_file" -O1 -fsanitize=undefined -fno-omit-frame-pointer

mutant_test()
{
	name=$1
	old=$2
	new=$3
	mutant="$temporary/$name.c"

	sed "s#$old#$new#" "$source_file" > "$mutant"
	if cmp -s "$source_file" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	if build_and_run "$name" "$mutant" -O2 >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

mutant_test source-tag \
	'request->bytes, shift, BM_MEM_RAM, &base, true' \
	'request->bytes, shift, BM_MEM_RESERVED, \&base, true'
mutant_test allocation-order \
	'request->bytes, shift, BM_MEM_RAM, &base, true' \
	'request->bytes, shift, BM_MEM_RAM, \&base, false'
mutant_test allocation-alignment \
	'const unsigned int shift = __builtin_ctzll(request->alignment);' \
	'const unsigned int shift = 12;'
mutant_test exact-retag \
	'memranges_insert(&candidate, base, request->bytes, request->tag);' \
	'memranges_insert(\&candidate, base, request->bytes + 4096, request->tag);'
mutant_test os-map-retag \
	'memranges_insert(&os_candidate, base, request->bytes, request->tag);' \
	'(void)os_candidate;'
mutant_test duplicate-registration \
	'if (!memcmp(&snapshots\[index\],' \
	'if (false \&\& !memcmp(\&snapshots[index],'
mutant_test late-registration \
	'bootmem_is_initialized() || request_count >' \
	'false || request_count >'

mkdir -p "$temporary/config" "$temporary/build"
printf '%s\n' \
	'CONFIG_VENDOR_EMULATION=y' \
	'CONFIG_BOARD_EMULATION_QEMU_X86_Q35=y' \
	'CONFIG_ANY_TOOLCHAIN=y' > "$temporary/config/.config"
make -C "$root" obj="$temporary/build" DOTCONFIG="$temporary/config/.config" \
	olddefconfig >/dev/null
! grep -q '^CONFIG_BOOTMEM_ALIGNED_RESERVATIONS=y$' \
	"$temporary/config/.config"

cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_BOOTMEM_ALIGNED_RESERVATIONS_SELECTOR
	bool
	default y
	select BOOTMEM_ALIGNED_RESERVATIONS
EOF
cp "$temporary/config/.config" "$temporary/config-selected"
make -C "$root" obj="$temporary/build-selected" \
	KBUILD_KCONFIG="$temporary/Kconfig" DOTCONFIG="$temporary/config-selected" \
	olddefconfig >/dev/null
grep -qx 'CONFIG_BOOTMEM_ALIGNED_RESERVATIONS=y' \
	"$temporary/config-selected"
make -C "$root" obj="$temporary/build-selected" \
	KBUILD_KCONFIG="$temporary/Kconfig" DOTCONFIG="$temporary/config-selected" \
	-j"$(getconf _NPROCESSORS_ONLN)" >/dev/null
nm -g --defined-only "$temporary/build-selected/ramstage/lib/bootmem.o" | \
	grep -q ' bootmem_aligned_reservation_register$'
nm -g --defined-only "$temporary/build-selected/ramstage/lib/bootmem.o" | \
	grep -q ' bootmem_aligned_reservations_register$'
nm -g --defined-only "$temporary/build-selected/ramstage/lib/bootmem.o" | \
	grep -q ' bootmem_aligned_reservation_query$'

if command -v qemu-system-x86_64 >/dev/null; then
	qemu_status=0
	timeout 10s qemu-system-x86_64 -M q35 -m 5G \
		-bios "$temporary/build-selected/coreboot.rom" -display none \
		-serial stdio -monitor none -no-reboot > "$temporary/qemu.log" 2>&1 || \
		qemu_status=$?
	test "$qemu_status" -eq 0 || test "$qemu_status" -eq 124
	grep -qi 'coreboot' "$temporary/qemu.log"
fi

printf '%s\n' 'aligned bootmem reservation tests: PASS'
