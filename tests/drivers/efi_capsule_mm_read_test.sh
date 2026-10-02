#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_DRIVERS_EFI_CAPSULE_MM_READ_ADMISSION 1' \
	'#define CONFIG_DRIVERS_EFI_CAPSULE_RAM_HANDOFF 1' \
	'#define CONFIG_DRIVERS_EFI_CAPSULE_ON_DISK_SUPPORT 1' \
	'#define CONFIG_PLATFORM_HAS_DRAM_CLEAR 1' \
	'#define CONFIG_ARCH_RAMSTAGE_X86_64 1' \
	'#define CONFIG_CPU_PT_ROM_MAP_GB 128' \
	'#define CONFIG_ECAM_MMCONF_BASE_ADDRESS 0xe0000000' \
	'#define CONFIG_SMMSTORE_BLOCK_SIZE 4096' > "$temporary/include/config.h"
compile()
{
	source=$1
	binary=$2
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wno-unused-parameter -Wno-sign-compare \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-pie -no-pie -ffunction-sections -fdata-sections \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-DCAPSULE_MM_READ_SOURCE="\"$source\"" \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/vendorcode/intel/edk2/UDK2017/MdePkg/Include" \
		-I"$root/src/vendorcode/intel/edk2/UDK2017/MdePkg/Include/X64" \
		-I"$root/src/vendorcode/intel/edk2/UDK2017/MdeModulePkg/Include" \
		"$root/tests/drivers/efi_capsule_mm_read_test.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/commonlib/region.c" \
		-Wl,--gc-sections -Wl,--wrap=rdev_readat -o "$binary"
}
for optimization in 0 2; do
	compile "$root/src/drivers/efi/capsules.c" "$temporary/normal"
	for scenario in 0 1 2 3 4 5 6 7 8; do
		ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/normal" "$scenario"
		printf 'capsule MM read O%s scenario %s: PASS\n' "$optimization" "$scenario"
	done
done

python3 - "$root/src/drivers/efi/capsules.c" "$temporary" <<'PY'
import pathlib
import sys
source = pathlib.Path(sys.argv[1]).read_text()
changes = {
    "pending": ("plan.action != PAYLOAD_MM_AUTHVAR_FTW_CLEAN", "false"),
    "schema": ("entry->attributes != 7", "false"),
    "dirty": ("capsule_store.dirty_tail_offset", "false"),
    "reread": (
        "memcpy(dest, data, entry->data_size);",
        "rdev_readat(rdev, dest, entry->data_offset + PAYLOAD_MM_AUTHVAR_FV_HEADER_SIZE, entry->data_size);",
    ),
}
for name, (before, after) in changes.items():
    if source.count(before) != 1:
        raise SystemExit(f"{name}: expected exactly one production match")
    pathlib.Path(sys.argv[2], name + ".c").write_text(source.replace(before, after))
PY
for optimization in 0 2; do
	for pair in pending:1 schema:5 dirty:6 reread:3; do
		name=${pair%:*}
		scenario=${pair#*:}
		compile "$temporary/$name.c" "$temporary/mutant"
		status=0
		ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
			"$temporary/mutant" "$scenario" >"$temporary/mutant.log" 2>&1 || status=$?
		[ "$status" = 134 ]
		if [ "$name" = reread ]; then
			grep -q 'assertion failed: reads == 1 && offset == 0' "$temporary/mutant.log"
		else
			grep -q 'assertion failed: mode_changes ==' "$temporary/mutant.log"
		fi
		! grep -Eq 'AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer|runtime error:' \
			"$temporary/mutant.log"
		printf 'capsule MM read O%s %s causal: PASS\n' "$optimization" "$name"
	done
done
