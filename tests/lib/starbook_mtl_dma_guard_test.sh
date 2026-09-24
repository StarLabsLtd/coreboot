#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '#define %s %s\n' CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 > \
	"$temporary/include/config.h"

for flags in '-O0' '-O2' '-O1 -fsanitize=address' \
	'-O1 -fsanitize=undefined'; do
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -fdata-sections -Wl,--gc-sections $flags \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/starbook_mtl_dma_guard_test.c" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c" \
		"$root/src/lib/payload_mm_authvar_mor_clear_plan.c" \
		-o "$temporary/test"
	for case_name in snapshot-builder policy prepare idempotent ensure-failure observe-failure \
		hardware-mutation invalid-observation random-failure zero-generation zero-identity \
		output-mutation ops-mutation ops-output-alias ops-plan-alias bind \
		bind-idempotent bind-token-mismatch bind-plan-mutation \
		bind-prepared-mutation bind-output-mutation bind-dma-mutation \
		bind-ops-mutation bind-prepared-output-alias bind-output-dma-alias \
		bind-plan-prepared-alias bound-prepare-idempotent bound-plan-change \
		seed-zero seeded-prepare; do
		"$temporary/test" "$case_name"
	done
done

printf '#define %s %s\n' CONFIG_STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD 1 > \
	"$temporary/include/config.h"
printf '#define %s %s\n' CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 >> \
	"$temporary/include/config.h"
"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin -O2 \
	-ffunction-sections -fdata-sections -Wl,--gc-sections \
	-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" \
	-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
	-I"$root/src/arch/x86/include" -I"$temporary/include" \
	"$root/tests/lib/starbook_mtl_dma_guard_test.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c" \
	"$root/src/lib/payload_mm_authvar_mor_clear_plan.c" \
	-o "$temporary/early-test"
"$temporary/early-test" early-seed-required
printf '#define %s %s\n' CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0 > \
	"$temporary/include/config.h"

mutant()
{
	name=$1
	expression=$2
	case_name=$3
	source="$temporary/$name.c"
	binary="$temporary/$name"

	sed "$expression" \
		"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c" > \
		"$source"
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin -O2 \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/starbook_mtl_dma_guard_test.c" "$source" \
		"$root/src/lib/payload_mm_authvar_mor_clear_plan.c" -o "$binary"
	if "$binary" "$case_name" >/dev/null 2>&1; then
		printf 'ERROR: %s mutation survived\n' "$name" >&2
		exit 1
	fi
}

mutant hardware-recheck \
	's/if (memcmp(&candidate, &recheck, sizeof(candidate)) ||/if (false ||/' \
	hardware-mutation
mutant plan-recheck \
	's/memcmp(&plan_copy, plan, sizeof(plan_copy)) ||/false ||/' bind-plan-mutation
mutant ops-recheck \
	's/memcmp(&ops_copy, ops, sizeof(ops_copy)) ||/false ||/' ops-mutation
mutant output-recheck \
	'/memcmp(snapshot, &(const struct starbook_mtl_dma_guard_snapshot)/,+1c\
\t    false)' output-mutation
mutant generation-required \
	's/if (!random_words\[0\])/if (false)/; s/!snapshot_copy.generation ||/false ||/' \
	zero-generation
mutant exclusion-reason \
	's/span->exclusion_reason == reason &&/(span->exclusion_reason == reason || true) \&\&/' \
	policy
mutant contiguous-geometry \
	's/end != snapshot->table.base ||/false ||/' \
	policy
mutant token-binding \
	's/plan_copy.inventory_generation != snapshot_copy.generation ||/false ||/' \
	bind-token-mismatch

cp "$root/src/Kconfig" "$temporary/Kconfig"
printf '\nconfig TEST_MTL_MOR_DMA_GUARD_SELECTOR\n\tbool\n\tdefault y\n\tselect STARLABS_STARBOOK_MTL_MOR_DMA_GUARD\n' >> \
	"$temporary/Kconfig"
cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/config"
printf '%s\n' 'CONFIG_ENABLE_EARLY_DMA_PROTECTION=y' >> "$temporary/config"
make -s -C "$root" KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config" obj="$temporary/obj" olddefconfig
for required in STARLABS_STARBOOK_MTL_MOR_DMA_GUARD \
	STARLABS_STARBOOK_MTL_DMA_LIVE_BACKEND PAYLOAD_MM_AUTHVAR_MOR_CLEAR_PLAN; do
	grep -q "^CONFIG_$required=y$" "$temporary/config"
done
for forbidden in PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT \
	PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT PAYLOAD_MM_AUTHVAR_MOR_POLICY \
	PAYLOAD_MM_AUTHVAR_CONTRACT SMMSTORE PAYLOAD_DMA_HANDOFF \
	STARLABS_STARBOOK_MTL_DMA_HANDOFF; do
	! grep -q "^CONFIG_$forbidden=y$" "$temporary/config"
done
makefile="$root/src/mainboard/starlabs/starbook/variants/mtl/Makefile.mk"
grep -Fq 'ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_LIVE_BACKEND) += dma_live.c' \
	"$makefile"
grep -Fq 'ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_LIVE_BACKEND) += dma_live_platform.c' \
	"$makefile"
grep -Fq 'ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_MOR_DMA_GUARD) += dma_guard.c' \
	"$makefile"
grep -Fq 'ramstage-$(CONFIG_STARLABS_STARBOOK_MTL_DMA_HANDOFF) += dma_live_handoff.c' \
	"$makefile"
! grep -q 'BOOT_STATE_INIT_ENTRY' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_guard.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c"
grep -q 'BOOT_STATE_INIT_ENTRY' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/dma_live_handoff.c"

printf '%s\n' 'StarBook MTL MOR DMA guard tests: PASS'
