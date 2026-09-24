#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
config="$root/build/tests/tests/lib/starbook-mtl-mor-clear-x86-binding-test"

make -C "$root" build-tests/lib/starbook-mtl-mor-clear-x86-binding-test >/dev/null

build_and_run()
{
	name=$1
	source=$2
	shift 2
	"${CC:-cc}" -std=gnu23 -Wall -Wextra -Werror -Wundef \
		-Wstrict-prototypes -fno-builtin -fno-pie -fno-pic "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -D__TEST_SRCOBJ__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$config" -I"$root/tests/include/mocks" -I"$root/tests/include" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$root/build/tests" \
		"$root/tests/lib/starbook_mtl_mor_clear_x86_binding_test.c" "$source" \
		-no-pie -o "$temporary/$name"
	"$temporary/$name"
}

source_file="$root/src/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.c"
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

mutant_test allow-s3 'resume_from_s3)' 'false)'
mutant_test wrong-page-type 'reservation->tag == request->tag' 'true'
mutant_test wrong-alignment '![(]reservation->base % request->alignment[)]' 'true'
mutant_test no-state-recheck 'memcmp[(]\&state, reservations, sizeof[(]state[)][)]' 'false'
mutant_test aperture-active-firmware \
	'PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED' \
	'PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE'
mutant_test stale-plan-on-invalid-binding \
	'if (plan_valid)' 'if (false)'
mutant_test stale-binding-on-invalid-plan \
	'if (binding_valid)' 'if (false)'
mutant_test no-authority-seal \
	'authority->seal == authority_seal(authority) &&' 'true &&'
mutant_test no-guard-revalidation \
	'!guard_revalidate(binding, MTL_MOR_CLEAR_BUSY, &authority)' 'false'
mutant_test omit-plan-lifetime-overlay \
	'[.]base = (uintptr_t)plan,' '.base = (uintptr_t)\&binding->backend,'
mutant_test replay-caller-binding \
	'if (!lifecycle_advance[(]' 'if (false \&\& !lifecycle_advance('

mkdir -p "$temporary/config-default" "$temporary/build-default"
cp "$root/configs/config.starlabs_starbook_mtl" \
	"$temporary/config-default/.config"
make -C "$root" obj="$temporary/build-default" \
	DOTCONFIG="$temporary/config-default/.config" olddefconfig >/dev/null
! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING=y$' \
	"$temporary/config-default/.config"

cp "$root/src/Kconfig" "$temporary/Kconfig"
cat >> "$temporary/Kconfig" <<'EOF'

config TEST_MTL_MOR_CLEAR_X86_BINDING_SELECTOR
	bool
	default y
	select ENABLE_EARLY_DMA_PROTECTION
	select STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING
EOF
mkdir -p "$temporary/config-selected" "$temporary/build-selected"
cp "$root/configs/config.starlabs_starbook_mtl" \
	"$temporary/config-selected/.config"
printf '%s\n' 'CONFIG_ANY_TOOLCHAIN=y' >> \
	"$temporary/config-selected/.config"
make -C "$root" obj="$temporary/build-selected" \
	KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-selected/.config" olddefconfig >/dev/null
for symbol in \
	STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING \
	BOOTMEM_ALIGNED_RESERVATIONS \
	PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT \
	PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT \
	PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR \
	PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND \
	STARLABS_STARBOOK_MTL_MOR_COLD_CLASSIFICATION \
	STARLABS_STARBOOK_MTL_MOR_EARLY_DMA_GUARD \
	STARLABS_STARBOOK_MTL_MOR_DMA_GUARD; do
	grep -qx "CONFIG_${symbol}=y" "$temporary/config-selected/.config"
done
make -C "$root" obj="$temporary/build-selected" \
	KBUILD_KCONFIG="$temporary/Kconfig" \
	DOTCONFIG="$temporary/config-selected/.config" -j4 \
	"$temporary/build-selected/ramstage/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.o" \
	"$temporary/build-selected/ramstage/mainboard/starlabs/starbook/variants/mtl/mor_live_inventory.o" \
	"$temporary/build-selected/ramstage/mainboard/starlabs/starbook/variants/mtl/dma_guard.o" \
	"$temporary/build-selected/ramstage/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.o" \
	"$temporary/build-selected/ramstage/lib/payload_mm_authvar_mor_clear_executor.o" \
	"$temporary/build-selected/ramstage/lib/payload_mm_authvar_mor_clear_x86.o" \
	"$temporary/build-selected/ramstage/lib/bootmem.o" >/dev/null
nm -g --defined-only \
	"$temporary/build-selected/ramstage/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.o" | \
	grep -q ' starbook_mtl_mor_clear_x86_prepare$'
nm -g --defined-only "$temporary/build-selected/ramstage/lib/bootmem.o" | \
	grep -q ' bootmem_aligned_reservations_register$'

echo 'StarBook MTL MOR x86 binding tests: PASS'
