#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

crossgcc=${XGCCPATH:-"$root/util/crossgcc/xgcc/bin"}
crossgcc=$(realpath "$crossgcc")
test -x "$crossgcc/i386-elf-gcc"
PATH="$crossgcc:$PATH"
export PATH

resolve_config()
{
	name=$1
	kconfig=$2
	input=$3
	build_name=${4:-$name}

	mkdir -p "$temporary/build-$build_name"
	cp "$input" "$temporary/config-$name"
	make -C "$root" -s obj="$temporary/build-$build_name" \
		KBUILD_KCONFIG="$kconfig" DOTCONFIG="$temporary/config-$name" \
		olddefconfig
}

# Explicitly disabling the hidden prerequisite must resolve to the exact same
# production selection as leaving it absent from the release configuration.
resolve_config default "$root/src/Kconfig" \
	"$root/configs/config.starlabs_starbook_mtl" release
cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/release-off"
printf '%s\n' \
	'# CONFIG_STARLABS_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES is not set' \
	>> "$temporary/release-off"
resolve_config explicit-off "$root/src/Kconfig" "$temporary/release-off" release
cmp "$temporary/config-default" "$temporary/config-explicit-off"

grep -qx 'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y' "$temporary/config-default"
grep -qx 'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y' "$temporary/config-default"
grep -qx 'CONFIG_SMMSTORE=y' "$temporary/config-default"
! grep -q '^CONFIG_CAPSULE_BROKER_CONTRACT=y$' "$temporary/config-default"
! grep -q '^CONFIG_CAPSULE_PLATFORM_FACTS=y$' "$temporary/config-default"
! grep -q '^CONFIG_CAPSULE_PLATFORM_ADAPTERS=y$' "$temporary/config-default"

cp "$root/src/Kconfig" "$temporary/Kconfig-selected"
cat >> "$temporary/Kconfig-selected" <<'EOF'

config TEST_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES
	def_bool y
	select STARLABS_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES
EOF
resolve_config selected "$temporary/Kconfig-selected" \
	"$root/configs/config.starlabs_starbook_mtl"

for symbol in \
	STARLABS_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES \
	CAPSULE_UPDATE_CONTRACT \
	PAYLOAD_MM_AUTHVAR_CONTRACT \
	CAPSULE_BROKER_CONTRACT \
	CAPSULE_BROKER_FIXED_BUFFERS \
	CAPSULE_BROKER_CBMEM_BUFFERS \
	CAPSULE_PLATFORM_FACTS \
	CAPSULE_PLATFORM_ADAPTERS \
	SPI_FLASH_SMM; do
	grep -qx "CONFIG_${symbol}=y" "$temporary/config-selected"
done

# Typed and legacy update paths must remain mutually exclusive.
! grep -q '^CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y$' \
	"$temporary/config-selected"
! grep -q '^CONFIG_SMMSTORE_FULL_FLASH_ACCESS=y$' \
	"$temporary/config-selected"
! grep -q '^CONFIG_CAPSULE_BROKER_ENDPOINT_PUBLICATION=y$' \
	"$temporary/config-selected"

selected_build="$temporary/build-selected"
make -C "$root" -j4 obj="$selected_build" \
	KBUILD_KCONFIG="$temporary/Kconfig-selected" \
	DOTCONFIG="$temporary/config-selected" \
	"$selected_build/ramstage/cpu/x86/smm/smm.manual" \
	> "$temporary/build-selected.log" 2>&1 || {
	cat "$temporary/build-selected.log" >&2
	exit 1
}

cross_nm=$(awk -F ':=' '$1 ~ /^[[:space:]]*NM_x86_32$/ {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
	print $2
	exit
}' "$selected_build/xcompile")
cross_nm=$(command -v "$cross_nm")
case $(realpath "$cross_nm") in
"$crossgcc"/*) ;;
*)
	printf 'ERROR: selected build used unpinned nm: %s\n' "$cross_nm" >&2
	exit 1
	;;
esac

archive_symbols="$temporary/smm-archive.symbols"
final_symbols="$temporary/smm-final.symbols"
"$cross_nm" -g --defined-only "$selected_build/smm/smm.a" > \
	"$archive_symbols"
"$cross_nm" -g --defined-only \
	"$selected_build/ramstage/cpu/x86/smm/smm.manual" > "$final_symbols"
# Selection must place the concrete implementations in the MTL SMM link input,
# but the final module must discard them until a reviewed composition calls
# them. Merely selecting prerequisites must not install a runtime service.
for symbol in \
	capsule_platform_facts_collect \
	capsule_platform_media_adapter_build \
	capsule_platform_smm_storage_contains \
	capsule_platform_identity_build; do
	awk -v symbol="$symbol" '$3 == symbol { found++ }
		END { exit found != 1 }' "$archive_symbols"
	! awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$final_symbols"
done

printf '%s\n' \
	'StarBook MTL dormant capsule platform prerequisites artifact test: PASS'
