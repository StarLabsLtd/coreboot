#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

scratch_make()
{
	env -u MAKELEVEL -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES \
		"${MAKE:-make}" "$@"
}

profile_kconfig="$temporary/Kconfig"
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_MTL_LOADER_INSTANCE_PROVIDER
	def_bool y
	select ENABLE_EARLY_DMA_PROTECTION
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_FAIL_STOP_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_INTEL_ADAPTER
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER
EOF

config="$temporary/provider.config"
build="$temporary/provider"
cp "$root/configs/config.starlabs_starbook_mtl" "$config"
"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
scratch_make -s -C "$root" KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" \
	obj="$build" olddefconfig
for symbol in STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE \
	STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY \
	STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER \
	SOC_INTEL_METEORLAKE_LOADER_INSTANCE_SOURCE \
	SOC_INTEL_METEORLAKE_LOADER_INSTANCE_REQUIRED \
	SOC_INTEL_METEORLAKE_LOADER_INSTANCE_AUTHORITY \
	SMM_INVOCATION_LOADER_INSTANCE; do
	grep -q "^CONFIG_${symbol}=y$" "$config"
done
scratch_make -s -C "$root" KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" \
	obj="$build" -B -j4 \
	"$build/romstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.o" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.o" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_authority.o" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.o"

printf '%s\n' \
	'#include "mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.h"' |
	${CC:-cc} -m32 -std=gnu11 -ffreestanding -fsyntax-only -x c - \
		-D__COREBOOT__ -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/commonlib/include" -I"$root/src/arch/x86/include"

authority="$build/ramstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_authority.o"
if nm -u "$authority" | grep -Eq 'mor_early_dma|dma_guard|dma_live|cbmem'; then
	printf '%s\n' 'provider-only authority retained a MOR adapter dependency' >&2
	exit 1
fi
if nm "$authority" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.o" |
	grep -q '__atomic_'; then
	printf '%s\n' '32-bit MTL loader-instance provider gained libatomic dependency' >&2
	exit 1
fi

for profile in starlabs_lite_glk starlabs_lite_adl; do
	off="$temporary/$profile.config"
	cp "$root/configs/config.$profile" "$off"
	"$root/util/scripts/config" --file "$off" -e ANY_TOOLCHAIN
	scratch_make -s -C "$root" DOTCONFIG="$off" obj="$temporary/$profile" \
		olddefconfig
	for symbol in STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE \
		STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY \
		STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER; do
		! grep -q "^CONFIG_${symbol}=y$" "$off"
	done
done

off_config="$temporary/mtl-off.config"
off_build="$temporary/mtl-off"
off_log="$temporary/mtl-off.log"
cp "$root/configs/config.starlabs_starbook_mtl" "$off_config"
"$root/util/scripts/config" --file "$off_config" -e ANY_TOOLCHAIN
scratch_make -s -C "$root" DOTCONFIG="$off_config" obj="$off_build" olddefconfig
scratch_make -C "$root" V=1 DOTCONFIG="$off_config" obj="$off_build" -B \
	"$off_build/romstage/soc/intel/meteorlake/romstage/romstage.o" \
	"$off_build/ramstage/soc/intel/meteorlake/chip.o" >"$off_log" 2>&1

compare_base_object()
{
	source=$1
	current=$2
	name=$3
	base_source="$temporary/$name.base.c"
	base_object="$temporary/$name.base.o"

	git -C "$root" show 3d6407d3cb6a430e6326a1bfb70ee525d3dec39d:"$source" > \
		"$base_source"
	command=$(grep -- "-c -o $current $source" "$off_log" | tail -1)
	test -n "$command"
	command=$(printf '%s\n' "$command" | \
		sed "s|-c -o $current $source|-c -o $base_object $base_source|")
	(cd "$root" && eval "$command")
	objcopy --strip-debug "$current" "$temporary/$name.current.stripped.o"
	objcopy --strip-debug "$base_object" "$temporary/$name.base.stripped.o"
	cmp "$temporary/$name.current.stripped.o" \
		"$temporary/$name.base.stripped.o"
}

compare_base_object src/soc/intel/meteorlake/romstage/romstage.c \
	"$off_build/romstage/soc/intel/meteorlake/romstage/romstage.o" romstage
compare_base_object src/soc/intel/meteorlake/chip.c \
	"$off_build/ramstage/soc/intel/meteorlake/chip.o" chip

if rg -q 'smm_load_module|APM_CNT|outb|lb_(new_record|add)|save_state' \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/loader_instance_authority.c" \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.c"; then
	printf '%s\n' 'loader-instance provider gained route or public-ABI coupling' >&2
	exit 1
fi
test "$(rg -l 'starbook_mtl_smm_invocation_loader_instance_take' \
	"$root/src" | wc -l)" -eq 2
if rg -q '^CONFIG_STARLABS_STARBOOK_MTL_(LOADER_INSTANCE_SOURCE|LOADER_INSTANCE_AUTHORITY|SMM_INVOCATION_LOADER_INSTANCE_PROVIDER)=y$' \
	"$root/configs"; then
	printf '%s\n' 'loader-instance provider is no longer default-off' >&2
	exit 1
fi
documentation="$root/Documentation/mainboard/starlabs/starbook-mtl-loader-instance.md"
grep -q 'not monotonic' "$documentation"
grep -q 'S0ix' "$documentation"
grep -q 'installs no SMM-loader callsite' "$documentation"

printf '%s\n' 'StarBook MTL loader-instance provider profiles: PASS'
