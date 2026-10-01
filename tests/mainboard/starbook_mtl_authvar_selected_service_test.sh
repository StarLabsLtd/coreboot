#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
cleanup()
{
	if [ "${KEEP_MTL_AUTHVAR_SELECTED_TMP:-0}" = 1 ]; then
		printf 'selected service artifacts: %s\n' "$temporary" >&2
	else
		rm -rf "$temporary"
	fi
}
trap cleanup EXIT HUP INT TERM

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make BUILD_TIMELESS=1 KERNELVERSION=coreboot-mtl-service-test \
		UPDATED_SUBMODULES=1 -C "$root" "$@"
)

common_git=$(realpath "$(git -C "$root" rev-parse --git-common-dir)")
intel_fsp=$(dirname "$(dirname "$common_git")")/intel_fsp
fsp_headers="$intel_fsp/arl/202507011953/Include/"
fsp_fd="$intel_fsp/arl/202507011953/Release/Fsp.fd"
test -d "$fsp_headers" && test -f "$fsp_fd"
fsp_headers=$(realpath --relative-to="$root" "$fsp_headers")
fsp_fd=$(realpath --relative-to="$root" "$fsp_fd")
test -f "$root/3rdparty/vboot/firmware/include/vb2_sha.h"
test -f "$root/3rdparty/mbedtls/library/asn1parse.c"
profile="$temporary/Kconfig"
# Kconfig requires mainmenu before the additional default overrides.
{
	sed -n '1,3p' "$root/src/Kconfig"
	sed -n '1,$p' "$root/tests/mainboard/starbook_mtl_authvar_selected_service.Kconfig"
	sed -n '4,$p' "$root/src/Kconfig"
} > "$profile"

configure()
{
	name=$1
	selected=$2
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make obj="$build" DOTCONFIG="$config" KBUILD_KCONFIG="$profile" \
		KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -e PAYLOAD_NONE \
		-d LTO -d SMMSTORE -d BOOTMEDIA_SMM_BWP_RUNTIME_OPTION \
		-d DRIVERS_EFI_UPDATE_CAPSULES -d DRIVERS_EFI_GENERATE_CAPSULE \
		--set-str FSP_HEADER_PATH "$fsp_headers" --set-str FSP_FD_PATH "$fsp_fd"
	if [ "$selected" = 1 ]; then
		"$root/util/scripts/config" --file "$config" \
			-e TEST_MTL_AUTHVAR_SERVICE_PREREQUISITES
	fi
	scratch_make obj="$build" DOTCONFIG="$config" KBUILD_KCONFIG="$profile" \
		olddefconfig >"$build/config.log" 2>&1
	if grep -qi 'unmet direct dependencies' "$build/config.log"; then
		cat "$build/config.log" >&2
		exit 1
	fi
}

configure OFF 0
! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_SERVICE_DISPATCH=y$' "$config"
! grep -q '^CONFIG_SMM_APMC_ROUTE_AUTHVAR_SERVICE=y$' "$config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED=y$' "$config"
if ! scratch_make obj="$build" DOTCONFIG="$config" KBUILD_KCONFIG="$profile" \
	-j4 "$build/smm/smm.elf" >"$build/smm-build.log" 2>&1; then
	cat "$build/smm-build.log" >&2
	exit 1
fi
! nm "$build/smm/smm.elf" | grep -q ' starbook_mtl_authvar_service_runtime_dispatch$'
test ! -e "$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_service_runtime_dispatch.o"

configure ON 1
for symbol in STARLABS_STARBOOK_MTL_AUTHVAR_SERVICE_DISPATCH \
	STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DISPATCH \
	STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION \
	STARLABS_STARBOOK_MTL_AUTHVAR_SMM_CAPACITY \
	PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP \
	PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER \
	SMM_APMC_ROUTE_AUTHVAR_SERVICE PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED \
	SMM_PRE_LOCK_DISPATCH BOOTMEDIA_SMM_BWP; do
	grep -qx "CONFIG_${symbol}=y" "$config"
done
grep -Eq '^CONFIG_SMM_MODULE_STACK_SIZE=(0x4000|16384)$' "$config"
grep -qx 'CONFIG_SMM_TSEG_SIZE=0x1000000' "$config"
! grep -q '^CONFIG_SMMSTORE=y$' "$config"
! grep -q '^CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION=y$' "$config"

# Link the actual selected SMM entry point, not an object with an injected
# reference or a host fixture substituting the platform admission callbacks.
if ! scratch_make obj="$build" DOTCONFIG="$config" KBUILD_KCONFIG="$profile" \
	STACK_AUDIT_CFLAGS='-fstack-usage -fcallgraph-info=su' -j4 "$build/smm/smm.elf" \
	>"$build/smm-build.log" 2>&1; then
	cat "$build/smm-build.log" >&2
	exit 1
fi
elf="$build/smm/smm.elf"
file "$elf" | grep -q 'ELF 32-bit'
for symbol in smm_pre_lock_dispatch \
	starbook_mtl_authvar_service_runtime_dispatch \
	platform_payload_mm_authvar_service_runtime_admitted \
	platform_payload_mm_authvar_service_finalize_admitted \
	payload_mm_authvar_service_execute \
	payload_mm_authvar_service_finalize \
	starbook_mtl_authvar_protected_region_read; do
	test "$(nm -g --defined-only "$elf" | awk -v symbol="$symbol" \
		'$2 == "T" && $3 == symbol { count++ } END { print count + 0 }')" -eq 1
done
! nm -u "$elf" | grep -Eq '__atomic|libatomic'
dispatcher="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.o"
nm -u "$dispatcher" | grep -q ' starbook_mtl_authvar_service_runtime_dispatch$'
test -s "$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_service_runtime_dispatch.o"
printf '%s\n' 'MTL selected protected service SMM linkage: PASS (no hardware admission claim)'
