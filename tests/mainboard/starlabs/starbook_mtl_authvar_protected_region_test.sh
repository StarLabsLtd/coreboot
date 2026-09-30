#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include/cpu/x86" "$temporary/include/device" \
 "$temporary/include/intelblocks"
printf '%s\n' '#pragma once' '#include <stdint.h>' \
 'typedef struct { uint32_t lo, hi; } msr_t;' \
 'msr_t rdmsr(unsigned int index);' > "$temporary/include/cpu/x86/msr.h"
printf '%s\n' '#pragma once' '#include <stdint.h>' \
 '#include <types.h>' '#include <device/pci_type.h>' \
 'uint32_t pci_read_config32(unsigned int device, unsigned int index);' \
 > "$temporary/include/device/pci_ops.h"
printf '%s\n' '#pragma once' '#define TSEG 0xb8' '#define BGSM 0xb4' \
 > "$temporary/include/intelblocks/systemagent.h"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
 '#define CONFIG_ROM_SIZE 8388608' > "$temporary/include/config.h"
for optimization in 0 2; do
 ${CC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
  -Wconversion -Wshadow -Wstrict-prototypes -fno-builtin -fno-pie -no-pie \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  -fno-omit-frame-pointer -D__COREBOOT__ -D__SMM__ \
  -include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
  -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
  -I"$temporary/include" -isystem "$root/src/include" \
  -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
  -I"$root/src/arch/x86/include" -I"$root/src/soc/intel/meteorlake/include" \
  -I"$root/src/soc/intel/common/block/include" \
  -I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
  "$root/tests/mainboard/starlabs/starbook_mtl_authvar_protected_region_test.c" \
  "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_protected_region.c" \
  -o "$temporary/test"
 ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 "$temporary/test"
done
mkdir "$temporary/native"
cp "$temporary/include/config.h" "$temporary/native/config.h"
# Compile the same implementation with real hardware-access declarations too.
# This object is never executed by the host fixture.
${CC:-cc} -std=gnu11 -Os -Wall -Wextra -Werror -fno-builtin -ffreestanding \
 -D__COREBOOT__ -D__SMM__ -include "$root/src/include/kconfig.h" \
 -include "$root/src/include/rules.h" \
 -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
 -isystem "$temporary/native" -isystem "$root/src/include" \
 -isystem "$root/src/commonlib/include" -isystem "$root/src/commonlib/bsd/include" \
 -isystem "$root/src/arch/x86/include" -isystem "$root/src/soc/intel/meteorlake/include" \
 -isystem "$root/src/soc/intel/common/block/include" \
 -c "$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_protected_region.c" \
 -o "$temporary/native.o"
${OBJDUMP:-objdump} -d "$temporary/native.o" > "$temporary/native.dis"
grep -q '[[:space:]]rdmsr' "$temporary/native.dis"
if grep -q '[[:space:]]wrmsr' "$temporary/native.dis"; then
 echo 'unexpected MSR programming in the read-only protection proof' >&2
 exit 1
fi
echo 'MTL executing-CPU live locked TSEG/SMRR region: PASS (hardware-read model)'
