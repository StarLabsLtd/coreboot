#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
common_git=$(realpath "$(git -C "$root" rev-parse --git-common-dir)")
intel_fsp=$(dirname "$(dirname "$common_git")")/intel_fsp
fsp_headers=$(realpath --relative-to="$root" "$intel_fsp/arl/202507011953/Include")
fsp_fd=$(realpath --relative-to="$root" "$intel_fsp/arl/202507011953/Release/Fsp.fd")

scratch_make()
{
	env -u MAKELEVEL -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES \
		make -s -C "$root" UPDATED_SUBMODULES=1 "$@"
}

config="$temporary/on.config"
build="$temporary/on"
cp "$root/configs/config.starlabs_starbook_mtl" "$config"
"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO \
	-e ENABLE_EARLY_DMA_PROTECTION \
	-e STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_CLASSIFICATION \
	--set-str FSP_HEADER_PATH "$fsp_headers" --set-str FSP_FD_PATH "$fsp_fd"
scratch_make DOTCONFIG="$config" obj="$build" olddefconfig
for symbol in STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_CLASSIFICATION \
	SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER \
	STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE \
	STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY; do
	rg -q "^CONFIG_${symbol}=y$" "$config"
done
for symbol in PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION \
	STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER \
	DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT; do
	! rg -q "^CONFIG_${symbol}=y$" "$config"
done
source_object="$build/romstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.o"
authority_object="$build/ramstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_authority.o"
scratch_make DOTCONFIG="$config" obj="$build" -j4 "$source_object" \
	"$authority_object" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_source.o" \
	"$build/romstage/soc/intel/meteorlake/romstage/romstage.o" \
	"$build/romstage/soc/intel/meteorlake/authvar_presence_boot_classifier.o"
nm "$authority_object" | rg -q ' T mainboard_authvar_presence_cold_boot$'
nm -u "$source_object" | rg -q 'mtl_authvar_presence_boot_classify$'

for board in starlabs_starbook_mtl starlabs_lite_adl starlabs_lite_glk; do
	off="$temporary/$board.config"
	cp "$root/configs/config.$board" "$off"
	"$root/util/scripts/config" --file "$off" -e ANY_TOOLCHAIN
	scratch_make DOTCONFIG="$off" obj="$temporary/$board" olddefconfig
	! rg -q '^CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_COLD_CLASSIFICATION=y$' "$off"
done
echo 'StarBook MTL presence cold source configuration and object builds: PASS'
