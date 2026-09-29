#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d); trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"; printf '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0\n' > "$temporary/include/config.h"
cases='valid lifecycle-arena rtaddr-mode partial-bus bme missing-igd tail
digest gfx-active gfx-mode live-drift live-aba pci-drift
misaligned-range misaligned-size misaligned-used
range-overflow null-table above-4g huge-table mmio-table forbidden-table
zero-loader-nonce zero-generation bad-lifecycle identity-reserved
unprotected-receipt unprotected-read unprotected-sha
unprotected-context unprotected-workspace wrong-runtime-view
workspace-context-alias receipt-toctou observer-toctou'

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined'; do
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin $flags -D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" -I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/starbook_mtl_dma_smm_policy_test.c" "$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c" -o "$temporary/test"
	for case_name in $cases; do
		"$temporary/test" "$case_name"
	done
done

"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin -O2 \
	-D__TEST__ -D__COREBOOT__ -D__SMM__ \
	-DSTARBOOK_MTL_DMA_SMM_POINTER_MAX=UINT32_MAX \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" \
	-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
	-I"$root/src/arch/x86/include" -I"$temporary/include" \
	"$root/tests/lib/starbook_mtl_dma_smm_policy_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c" \
	-o "$temporary/test32"
"$temporary/test32" pointer32

mutant()
{
	name=$1; expression=$2; case_name=$3
	sed "$expression" "$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c" > "$temporary/$name.c"
	! cmp -s "$temporary/$name.c" "$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c"
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin -O2 \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" -I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/starbook_mtl_dma_smm_policy_test.c" "$temporary/$name.c" -o "$temporary/$name"
	if "$temporary/$name" "$case_name" >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2; exit 1
	fi
}
mutant lifecycle-exclusion '/static bool overlaps/,/^}/c\
static bool overlaps(uint64_t base, uint64_t size, const struct starbook_mtl_dma_smm_range *range) { (void)base; (void)size; (void)range; return false; }' lifecycle-arena
mutant table-integrity \
	's/memcmp(digest, receipt->table_digest, 32)/false/' digest
mutant rtaddr-mode-bits \
	's/rtaddr != receipt->vtvc0_rtaddr/(rtaddr \& PLATFORM_VTBAR_ADDRESS_MASK) != (receipt->vtvc0_rtaddr \& PLATFORM_VTBAR_ADDRESS_MASK)/; s/receipt->vtvc0_rtaddr != receipt->tables.base/(receipt->vtvc0_rtaddr \& PLATFORM_VTBAR_ADDRESS_MASK) != receipt->tables.base/' \
	rtaddr-mode

grep -q '&receipt->handoff' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c"
grep -q '&receipt->arenas\[2\]' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c"

"${CC:-cc}" -std=gnu11 -O2 -fstack-usage -D__TEST__ -D__COREBOOT__ -D__SMM__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/mainboard/starlabs/starbook/variants/mtl" -I"$root/src/arch/x86/include" -I"$temporary/include" \
	-c "$root/src/mainboard/starlabs/starbook/variants/mtl/dma_smm_policy.c" -o "$temporary/stack.o"
awk -F '\t' '$1 ~ /starbook_mtl_dma_smm_verify$/ && $2 > 512 { exit 1 }' "$temporary/stack.su"
cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/config"
"$root/util/scripts/config" --file "$temporary/config" \
	-e STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY
make -s -C "$root" DOTCONFIG="$temporary/config" obj="$temporary/obj" \
	olddefconfig >/dev/null
! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY=y$' "$temporary/config"
printf '%s\n' 'StarBook MTL SMM DMA receipt tests: PASS'
