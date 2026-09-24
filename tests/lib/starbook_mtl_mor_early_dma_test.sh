#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0\n' > "$temporary/include/config.h"

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined'; do
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin $flags \
		-D__TEST__ -D__COREBOOT__ -D__BOOTBLOCK__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/starbook_mtl_mor_early_dma_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_early_dma.c" \
		-o "$temporary/test"
	for scenario in valid zero-generation bme order tail input-alias output-scratch \
		snapshot-drift generation-drift primary-mutation mirror-mutation; do
		"$temporary/test" "$scenario"
	done
done

mutant()
{
	name=$1
	expression=$2
	scenario=$3
	source="$temporary/$name.c"
	binary="$temporary/$name"

	sed "$expression" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_early_dma.c" > \
		"$source"
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin -O2 \
		-D__TEST__ -D__COREBOOT__ -D__BOOTBLOCK__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		"$root/tests/lib/starbook_mtl_mor_early_dma_test.c" "$source" \
		-o "$binary"
	if "$binary" "$scenario" >/dev/null 2>&1; then
		printf 'ERROR: %s mutation survived\n' "$name" >&2
		exit 1
	fi
}

mutant bme-check \
	's/(function->command & PCI_COMMAND_MASTER)/false/' bme
mutant ordering-check \
	's/(index && function\[-1\].bdf >= function->bdf)/false/' order
mutant mirror-check \
	's/memcmp(&record->primary, &record->mirror, sizeof(record->primary))/false/' \
	mirror-mutation

cp "$root/src/Kconfig" "$temporary/Kconfig"
printf '\nconfig TEST_MTL_MOR_EARLY_DMA_SELECTOR\n\tbool\n\tdefault y\n\tselect STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD\n' >> \
	"$temporary/Kconfig"
cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/config"
printf '%s\n' 'CONFIG_ENABLE_EARLY_DMA_PROTECTION=y' >> "$temporary/config"
make -s -C "$root" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config" obj="$temporary/obj" olddefconfig
for required in STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD \
	STARLABS_STARBOOK_MTL_MOR_COLD_CLASSIFICATION \
	STARLABS_STARBOOK_MTL_MOR_DMA_GUARD \
	SOC_INTEL_METEORLAKE_MOR_EARLY_DMA_GUARD; do
	grep -q "^CONFIG_$required=y$" "$temporary/config"
done
for forbidden in PAYLOAD_MM_AUTHVAR_MOR_POLICY PAYLOAD_MM_AUTHVAR_CONTRACT \
	SMMSTORE STARLABS_STARBOOK_MTL_DMA_HANDOFF; do
	! grep -q "^CONFIG_$forbidden=y$" "$temporary/config"
done

chip="$root/src/soc/intel/meteorlake/chip.c"
guard_line=$(grep -n 'mainboard_mor_early_dma_prepare()' "$chip" | cut -d: -f1)
fsps_line=$(grep -n '^[[:space:]]*fsp_silicon_init()' "$chip" | cut -d: -f1)
test "$guard_line" -lt "$fsps_line"
test "$(sed -n "$((guard_line + 1)),$((fsps_line - 1))p" "$chip" | \
	grep -Ec '[[:alnum:]_]\(' || true)" -eq 0
! grep -q 'BOOT_STATE_INIT_ENTRY' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/mor_early_dma.c"

printf '%s\n' 'StarBook MTL MOR early DMA tests: PASS'
