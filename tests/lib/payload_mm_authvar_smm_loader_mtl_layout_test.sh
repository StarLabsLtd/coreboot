#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"

cp "$root/src/Kconfig" "$tmp/Kconfig"
cat >> "$tmp/Kconfig" <<'EOF'

config TEST_MTL_AUTHVAR_SMM_COMPOSITION
	def_bool y
	select PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP
	select PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND
	select PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER
	select PAYLOAD_MM_AUTHVAR_COORDINATOR
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_SEAL
	select PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT
	select PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE
	select PAYLOAD_MM_AUTHVAR_BUNDLE_PLAN
	select PAYLOAD_MM_AUTHVAR_VOLATILE_VIEW
	select PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS
	select PAYLOAD_MM_AUTHVAR_STORE_SCANNER
	select PAYLOAD_MM_AUTHVAR_CERTDB
	select PAYLOAD_MM_AUTHVAR_AUTHORITY
	select PAYLOAD_MM_AUTHVAR_ROUTE
	select PAYLOAD_MM_AUTHVAR_FORMAT_PARSER
	select PAYLOAD_MM_AUTHVAR_EXECUTOR
	select PAYLOAD_MM_AUTHVAR_WRITER
	select PAYLOAD_MM_AUTHVAR_MEDIA_PORT
	select PAYLOAD_MM_AUTHVAR_FTW_DECODER
	select PAYLOAD_MM_AUTHVAR_CONTRACT
	select PAYLOAD_MM_AUTHVAR_SIGNATURE_DB
	select PAYLOAD_MM_CMS_CORE
	select PAYLOAD_MM_AUTHVAR_CANDIDATE
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_GRANT
EOF
cat > "$tmp/.config" <<'EOF'
CONFIG_VENDOR_STARLABS=y
CONFIG_BOARD_STARLABS_STARBOOK_MTL=y
CONFIG_ANY_TOOLCHAIN=y
CONFIG_TEST_MTL_AUTHVAR_SMM_COMPOSITION=y
# CONFIG_SMMSTORE is not set
EOF

make -s -C "$root" KBUILD_KCONFIG="$tmp/Kconfig" \
	DOTCONFIG="$tmp/.config" obj="$tmp/out" olddefconfig >/dev/null
grep -Fqx 'CONFIG_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP=y' "$tmp/.config"
grep -Fqx 'CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_SMM_CAPACITY=y' "$tmp/.config"
grep -Fqx 'CONFIG_SMM_TSEG_SIZE=0x1000000' "$tmp/.config"
! grep -Fqx 'CONFIG_SMMSTORE=y' "$tmp/.config"
make -s -C "$root" KBUILD_KCONFIG="$tmp/Kconfig" \
	DOTCONFIG="$tmp/.config" obj="$tmp/out" \
	"$tmp/out/smm/smm.elf-ldflags=-u payload_mm_authvar_smm_bootstrap_install" \
	"$tmp/out/smm/smm.elf" "$tmp/out/smmstub/smmstub.elf" -j4 >/dev/null
test -s "$tmp/out/smm/smm.elf"
test -s "$tmp/out/smmstub/smmstub.elf"

for symbol in payload_mm_authvar_smm_bootstrap_install \
	payload_mm_authvar_executor_install \
	payload_mm_authvar_authority_install \
	payload_mm_authvar_smmstore_install \
	payload_mm_authvar_mor_seal_channel_install; do
	nm -g --defined-only "$tmp/out/smm/smm.elf" | grep -Eq " T $symbol$"
done

load_values()
{
	readelf -lW "$1" | awk '$1 == "LOAD" { print $6, $8 }'
}

set -- $(load_values "$tmp/out/smm/smm.elf")
test "$#" -eq 2
handler_size=$1
handler_alignment=$2
set -- $(load_values "$tmp/out/smmstub/smmstub.elf")
test "$#" -eq 2
stub_size=$1
stub_alignment=$2

# Force review when the linked production inputs to the exact layout drift.
test "$handler_size" = 0x0e648
test "$handler_alignment" = 0x20
test "$stub_size" = 0x001c0
test "$stub_alignment" = 0x4

config_value()
{
	sed -n "s/^CONFIG_$1=//p" "$tmp/.config"
}

cpu_count=$(config_value MAX_CPUS)
stack_size=$(config_value SMM_MODULE_STACK_SIZE)
ied_size=$(config_value IED_REGION_SIZE)
cache_size=$(config_value SMM_RESERVED_SIZE)
opal_size=$(config_value SMM_OPAL_S3_STATE_SMRAM_SIZE)
test "$cpu_count" = 22
test "$stack_size" = 0x800
test "$ied_size" = 0x400000
test "$cache_size" = 0x200000
test "$opal_size" = 0x1000

cat > "$tmp/include/config.h" <<EOF
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_MAX_CPUS $cpu_count
EOF

for tseg_size in 0x800000U 0x1000000U; do
	for optimization in 0 2; do
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -D__TEST__ -D__COREBOOT__ \
			-DMTL_TSEG_SIZE="$tseg_size" \
			-DMTL_IED_SIZE="$ied_size" -DMTL_CACHE_SIZE="$cache_size" \
			-DMTL_OPAL_SIZE="$opal_size" -DMTL_CPU_COUNT="$cpu_count" \
			-DMTL_STACK_SIZE="$stack_size" \
			-DMTL_HANDLER_SIZE="$handler_size" \
			-DMTL_HANDLER_ALIGNMENT="$handler_alignment" \
			-DMTL_STUB_SIZE="$stub_size" \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" -I"$tmp/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_smm_loader_mtl_layout_test.c" \
			"$root/src/lib/payload_mm_authvar_smm_loader.c" \
			-o "$tmp/mtl-layout-$tseg_size-O$optimization"
		"$tmp/mtl-layout-$tseg_size-O$optimization"
	done
done
