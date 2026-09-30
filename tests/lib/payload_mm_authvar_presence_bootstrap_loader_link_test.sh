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

# No production Kconfig rewrite, fabricated cold fact or true opt-in provider.
{
	head -n 3 "$root/src/Kconfig"
	printf '\nsource "tests/lib/payload_mm_authvar_presence_bootstrap_loader_test.Kconfig"\n'
	tail -n +4 "$root/src/Kconfig"
} > "$temporary/Kconfig"
cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/config"
"$root/util/scripts/config" --file "$temporary/config" -e ANY_TOOLCHAIN \
	-d LTO -d SBOM -e PAYLOAD_NONE \
	--set-str FSP_HEADER_PATH "$fsp_headers" --set-str FSP_FD_PATH "$fsp_fd"
env -u MAKELEVEL -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES \
	make -s -C "$root" UPDATED_SUBMODULES=1 DOTCONFIG="$temporary/config" \
	obj="$temporary/build" KBUILD_KCONFIG="$temporary/Kconfig" olddefconfig
for symbol in PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER \
	PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION SMM_INVOCATION_RUNTIME_BINDING; do
	rg -q "^CONFIG_${symbol}=y$" "$temporary/config"
done
rg -q '^CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION=y$' "$temporary/config"
env -u MAKELEVEL -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES \
	make -s -C "$root" -j4 UPDATED_SUBMODULES=1 DOTCONFIG="$temporary/config" \
	obj="$temporary/build" KBUILD_KCONFIG="$temporary/Kconfig" \
	> "$temporary/build.log" 2>&1 || {
		tail -n 60 "$temporary/build.log"
		exit 1
	}
nm "$temporary/build/smm/cpu/x86/smm/smm_module_handler.o" |
	rg -q ' T smm_get_payload_mm_authvar_presence_bootstrap$'
if test "${PRESENCE_BOOTSTRAP_AUTHORITY_POLICY_LINK:-0}" = 1; then
	nm "$temporary/build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_authority_policy.o" |
		rg -q ' T starbook_mtl_authvar_presence_authority_policy_get$'
	if test "${PRESENCE_BOOTSTRAP_PRIVATE_CALLER_LINK:-0}" = 1; then
		nm "$temporary/build/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_sender.o" |
			rg -q ' T lb_board$'
		nm -u "$temporary/build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.o" |
			rg -q 'starbook_mtl_presence_bootstrap_route_install$'
		nm -u "$temporary/build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_bootstrap_receiver.o" |
			rg -q 'starbook_mtl_authvar_presence_route_composition_provision$'
		echo 'Canonical presence actual private dispatcher/factory callers: PASS (opt-in absent)'
	fi
	echo 'Canonical presence actual authority policy object: PASS (opt-in absent)'
fi
nm -u "$temporary/build/ramstage/cpu/x86/smm/smm_module_loader.o" |
	rg -q 'payload_mm_authvar_presence_tuple_sender_loader_provision$'
nm "$temporary/build/ramstage/lib/payload_mm_authvar_presence_publication.o" |
	rg -q ' W platform_payload_mm_authvar_presence_required$'
test -s "$temporary/build/coreboot.rom"
echo 'Canonical presence actual loader/handler ROM link: PASS (opt-in absent)'
