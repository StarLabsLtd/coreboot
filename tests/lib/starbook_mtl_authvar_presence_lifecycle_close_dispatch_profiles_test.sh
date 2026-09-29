#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "$root/../.mtl-lifecycle-close-dispatch.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
common_git=$(realpath "$(git -C "$root" rev-parse --git-common-dir)")
intel_fsp=$(dirname "$(dirname "$common_git")")/intel_fsp
fsp_headers="$intel_fsp/arl/202507011953/Include/"
fsp_fd="$intel_fsp/arl/202507011953/Release/Fsp.fd"
test -d "$fsp_headers" && test -f "$fsp_fd"
current_fsp_headers=$(realpath --relative-to="$root" "$fsp_headers")
current_fsp_fd=$(realpath --relative-to="$root" "$fsp_fd")

scratch_make()
(
	tree=$1
	shift
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make BUILD_TIMELESS=1 KERNELVERSION=coreboot-mtl-close-dispatch \
		-C "$tree" "$@"
)

profile="$temporary/Kconfig"
head -n 3 "$root/src/Kconfig" > "$profile"
cat >> "$profile" <<'EOF'

config TEST_MTL_LIFECYCLE_CLOSE_DISPATCH
	bool "test-only MTL lifecycle-close pre-lock dispatcher"
	default n
	select BOOTMEM_ALIGNED_RESERVATIONS
	select BOOTMEM_ALIGNED_RESERVATION_RECEIPT
	select ENABLE_EARLY_DMA_PROTECTION
	select PAYLOAD_MM_CMS_CORE
	select PAYLOAD_MM_AUTHVAR_CONTRACT
	select PAYLOAD_MM_AUTHVAR_STORE_SCANNER
	select PAYLOAD_MM_AUTHVAR_FTW_DECODER
	select PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS
	select PAYLOAD_MM_AUTHVAR_MEDIA_PORT
	select PAYLOAD_MM_AUTHVAR_WRITER
	select PAYLOAD_MM_AUTHVAR_EXECUTOR
	select PAYLOAD_MM_AUTHVAR_FORMAT_PARSER
	select PAYLOAD_MM_AUTHVAR_SIGNATURE_DB
	select PAYLOAD_MM_AUTHVAR_ROUTE
	select PAYLOAD_MM_AUTHVAR_AUTHORITY
	select PAYLOAD_MM_AUTHVAR_BUNDLE_PLAN
	select PAYLOAD_MM_AUTHVAR_CANDIDATE
	select PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT
	select PAYLOAD_MM_AUTHVAR_COORDINATOR
	select PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER
	select PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY
	select PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_OWNER
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_INTEL_ADAPTER
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER
	select SMM_INVOCATION_LOADER_COMPOSITION
	select SMM_INVOCATION_RUNTIME_VIEW
	select SMM_INVOCATION_INTEL_ADAPTER_PROVIDER
	select SMM_INVOCATION_INTEL_CAUSE
	select SMM_APMC_COMPOSITION_ATTESTED
	select SMM_APMC_COMMAND_REGISTRY
	select STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER
	select STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL
	select STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DISPATCH
	select STARLABS_STARBOOK_MTL_PAYLOAD_RESOURCE_HANDOFF
	select STARLABS_STARBOOK_MTL_DMA_HANDOFF
	select STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_MTL_LIFECYCLE_CLOSE_DISPATCH
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile"

lib_kconfig="$temporary/lib.Kconfig"
sed -e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 s/^\tdefault n$/\tdefault y if TEST_MTL_LIFECYCLE_CLOSE_DISPATCH\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF$/,+2 s/^\tdefault n$/\tdefault y if TEST_MTL_LIFECYCLE_CLOSE_DISPATCH\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 s/^\tdefault n$/\tdefault y if TEST_MTL_LIFECYCLE_CLOSE_DISPATCH\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" "$profile"

build="$temporary/on"
config="$build/full.config"
mkdir -p "$build"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl \
	defconfig >/dev/null
"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
	-e TEST_MTL_LIFECYCLE_CLOSE_DISPATCH -d LTO \
	--set-str FSP_HEADER_PATH "$current_fsp_headers" \
	--set-str FSP_FD_PATH "$current_fsp_fd"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" olddefconfig >/dev/null
for symbol in STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DISPATCH \
	STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DMA_POLICY \
	STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION \
	BOOTMEM_ALIGNED_RESERVATION_RECEIPT \
	SOC_INTEL_COMMON_BLOCK_VTD_TRANSLATION_VERIFY \
	SMM_PRE_LOCK_DISPATCH SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE \
	SMM_INVOCATION_INTEL_CAUSE SMM_INVOCATION_RUNTIME_BINDING; do
	grep -q "^CONFIG_${symbol}=y$" "$config"
done
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" STACK_AUDIT_CFLAGS=-fstack-usage -j4 \
	"$build/smm/smm" "$build/cbfs/fallback/ramstage.debug" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_sender.o" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.o" \
	>/dev/null

dispatcher="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.o"
authority="$build/smm/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.o"
receiver="$build/smm/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.o"
verifier="$build/smm/soc/intel/common/block/vtd/vtd_translation_verify.o"
sender="$build/ramstage/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_sender.o"
platform="$build/ramstage/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.o"
handler="$build/smm/cpu/x86/smm/smm_module_handler.o"
policy="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_policy.o"
loader="$build/ramstage/cpu/x86/smm/smm_module_loader.o"
backing="$build/ramstage/lib/payload_mm_authvar_presence_lifecycle_close_backing.o"
test -s "$dispatcher" && test -s "$authority" && test -s "$receiver" &&
	test -s "$verifier" && test -s "$sender" && test -s "$platform" &&
	test -s "$handler" && test -s "$policy" && test -s "$loader" &&
	test -s "$backing"
file "$dispatcher" "$authority" "$receiver" "$verifier" "$sender" \
	"$platform" "$handler" "$policy" "$loader" "$backing" |
	grep -c 'ELF 32-bit' | grep -q '^10$'
test "$(nm --defined-only "$dispatcher" | awk \
	'$3 == "smm_pre_lock_dispatch" { n++ } END { print n + 0 }')" -eq 1
nm -u "$handler" | grep -q 'smm_pre_lock_dispatch'
nm -u "$dispatcher" | grep -q 'intel_smm_invocation_private_apmc_cause'
nm -u "$dispatcher" | grep -q 'starbook_mtl_dma_smm_binding_get'
nm -u "$authority" | grep -q 'vtd_translation_verify'
nm --defined-only "$policy" |
	grep -q 'starbook_mtl_authvar_presence_lifecycle_close_install_policy'
nm --defined-only "$handler" |
	grep -q 'smm_get_payload_mm_authvar_presence_lifecycle_close_backing_verifier'
nm -u "$loader" |
	grep -q 'payload_mm_authvar_presence_lifecycle_close_backing_verifier_take'
nm --defined-only "$backing" |
	grep -q 'payload_mm_authvar_presence_lifecycle_close_backing_attest'
if nm -u "$dispatcher" | grep -Eq '__atomic|__sync|libatomic'; then
	echo 'lifecycle-close dispatcher gained runtime atomic dependency' >&2
	exit 1
fi
usage=$(find "$build/smm/mainboard/starlabs/starbook/variants/mtl" \
	-name '*authvar_presence_lifecycle_close_dispatch*.su' -print -quit)
test -n "$usage"
frame=$(awk -F '\t' '$1 ~ /smm_pre_lock_dispatch$/ { print $2 }' "$usage")
test -n "$frame" && test "$frame" -le 2048

# The hook must precede the legacy lock, and EOS must exist only in the BSP
# result arm. These source checks also kill accidental fallthrough mutations.
handler_source="$root/src/cpu/x86/smm/smm_module_handler.c"
dispatch_line=$(grep -n 'smm_pre_lock_dispatch(cpu' "$handler_source" | cut -d: -f1)
lock_line=$(grep -n 'if (!smi_obtain_lock())' "$handler_source" | cut -d: -f1)
test "$dispatch_line" -lt "$lock_line"
sed -n '/const enum smm_pre_lock_dispatch_result/,/^#endif/p' "$handler_source" > \
	"$temporary/hook"
test "$(grep -c 'southbridge_smi_set_eos' "$temporary/hook")" -eq 1
grep -A8 'SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED' "$temporary/hook" | \
	grep -q 'return;'
grep -A8 'SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED' "$temporary/hook" | \
	grep -q 'southbridge_smi_set_eos'
grep -q 'actual_canary != expected_canary' "$temporary/hook"
grep -A2 'default:' "$temporary/hook" | grep -q \
	'smm_invocation_platform_fail_stop'

baseline="$temporary/base"
mkdir -p "$baseline"
git -C "$root" archive 3d1cbb278c5 | tar -x -C "$baseline"
for module in vboot stm; do
	rmdir "$baseline/3rdparty/$module"
	ln -s "$root/3rdparty/$module" "$baseline/3rdparty/$module"
done
ln -s "$intel_fsp" "$temporary/intel_fsp"

build_off()
{
	tree=$1
	name=$2
	output="$temporary/$name"
	config="$output/full.config"
	tree_fsp_headers=$(realpath --relative-to="$tree" "$fsp_headers")
	tree_fsp_fd=$(realpath --relative-to="$tree" "$fsp_fd")
	mkdir -p "$output"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
	"$tree/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO \
		-d PAYLOAD_SEABIOS -e PAYLOAD_NONE \
		--set-str FSP_HEADER_PATH "$tree_fsp_headers" \
		--set-str FSP_FD_PATH "$tree_fsp_fd"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		olddefconfig >/dev/null
	if [ "$tree" = "$root" ]; then
		if grep -q '^CONFIG_SMM_PRE_LOCK_DISPATCH=y$' "$config" ||
		   grep -q '^CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DISPATCH=y$' "$config"; then
			echo 'default-off lifecycle-close profile unexpectedly enabled' >&2
			exit 1
		fi
	fi
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		-j4 "$output/smm/smm" >/dev/null
}
build_off "$root" current-off
build_off "$baseline" base-off
cmp "$temporary/current-off/smm/smm" "$temporary/base-off/smm/smm"
test ! -e "$temporary/current-off/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.o"

echo "StarBook MTL lifecycle-close dispatch profiles: PASS (frame $frame bytes)"
