#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

build_and_run() {
	name=$1
	shift
	cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -fno-builtin \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/soc/amd/common/block/include" -I"$temporary/include" \
		"$root/tests/soc/amd/iommu_runtime_standalone_test.c" \
		"$root/src/soc/amd/common/block/iommu/iommu_runtime.c" \
		"$root/src/soc/amd/common/block/iommu/iommu_dma.c" \
		-Wl,--gc-sections "$@" -o "$temporary/$name"
	"$temporary/$name"
}

build_and_run ordinary
build_and_run optimized -O2
build_and_run sanitized -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
printf '%s\n' 'AMD IOMMU runtime ordinary/O2/ASan+UBSan tests: PASS'
