#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "$root/../.mtl-lifecycle-close-dispatch.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

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
	select STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION
	select STARLABS_STARBOOK_MTL_LIFECYCLE_MAILBOX_AUTHORITY
	select SOC_INTEL_COMMON_BLOCK_VTD_TRANSLATION
	select PAYLOAD_MM_CMS_CORE

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
	-e TEST_MTL_LIFECYCLE_CLOSE_DISPATCH -d LTO
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" olddefconfig >/dev/null
for symbol in STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DISPATCH \
	SMM_PRE_LOCK_DISPATCH SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE \
	SMM_INVOCATION_INTEL_CAUSE SMM_INVOCATION_RUNTIME_BINDING \
	STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION \
	STARLABS_STARBOOK_MTL_DMA_SMM_REQUESTER_AUTHORITY \
	STARLABS_STARBOOK_MTL_LIFECYCLE_MAILBOX_AUTHORITY \
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY \
	SOC_INTEL_COMMON_BLOCK_VTD_TRANSLATION_VERIFY; do
	grep -q "^CONFIG_${symbol}=y$" "$config"
done
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" STACK_AUDIT_CFLAGS=-fstack-usage -j4 \
	"$build/smm/smm" \
	"$build/ramstage/cpu/x86/smm/smm_module_loader.o" \
	"$build/ramstage/lib/payload_mm_authvar_presence_lifecycle_close_backing.o" \
	"$build/ramstage/lib/payload_mm_authvar_presence_lifecycle_close_provider.o" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.o" \
	"$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.o" \
	>/dev/null

dispatcher="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.o"
handler="$build/smm/cpu/x86/smm/smm_module_handler.o"
authority="$build/smm/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.o"
requesters="$build/smm/mainboard/starlabs/starbook/variants/mtl/dma_smm_requester_authority.o"
verifier="$build/smm/soc/intel/common/block/vtd/vtd_translation_verify.o"
dma_receiver="$build/smm/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_receiver.o"
mailbox="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_mailbox.o"
install_receiver="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.o"
loader="$build/ramstage/cpu/x86/smm/smm_module_loader.o"
backing="$build/ramstage/lib/payload_mm_authvar_presence_lifecycle_close_backing.o"
provider="$build/ramstage/lib/payload_mm_authvar_presence_lifecycle_close_provider.o"
sender="$build/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.o"
test -s "$dispatcher" && test -s "$handler" && test -s "$authority" && \
	test -s "$requesters" && test -s "$verifier" && test -s "$dma_receiver" && \
	test -s "$mailbox" && test -s "$loader" && test -s "$backing" && \
	test -s "$provider" && test -s "$sender" && test -s "$install_receiver"
file "$dispatcher" "$handler" "$authority" "$requesters" "$verifier" \
	"$dma_receiver" "$mailbox" "$install_receiver" "$loader" "$backing" \
	"$provider" "$sender" | grep -c 'ELF 32-bit' | grep -q '^12$'
test "$(nm --defined-only "$dispatcher" | awk \
	'$3 == "smm_pre_lock_dispatch" { n++ } END { print n + 0 }')" -eq 1
nm -u "$handler" | grep -q 'smm_pre_lock_dispatch'
nm -u "$dispatcher" | grep -q 'intel_smm_invocation_private_apmc_cause'
! nm -u "$dispatcher" | grep -Eq '__atomic|__sync|libatomic'
nm --defined-only "$authority" | grep -q 'starbook_mtl_dma_smm_authority_verify'
nm --defined-only "$requesters" | grep -q \
	'starbook_mtl_dma_requester_authority_derive'
nm --defined-only "$verifier" | grep -q 'vtd_translation_verify'
nm --defined-only "$dma_receiver" | grep -q 'starbook_mtl_dma_smm_binding_get'
nm --defined-only "$mailbox" | grep -q \
	'starbook_mtl_lifecycle_mailbox_dma_protected'
nm --defined-only "$install_receiver" | grep -q \
	'starbook_mtl_authvar_presence_lifecycle_close_install_receive'
nm --defined-only "$handler" | grep -q \
	'smm_get_payload_mm_authvar_presence_lifecycle_close_mailbox_authority'
nm -u "$loader" | grep -q \
	'payload_mm_authvar_presence_lifecycle_close_mailbox_loader_provision'
nm --defined-only "$backing" | grep -q \
	'payload_mm_authvar_presence_lifecycle_close_backing_take_authenticated'
nm -u "$provider" | grep -q \
	'payload_mm_authvar_presence_lifecycle_close_backing_take_authenticated'
! nm -u "$authority" | grep -Eq '__atomic_load_8|libatomic'
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
git -C "$root" archive 591aafeea2fa6b50086982b05fe0123d4ade3ea6 | \
	tar -x -C "$baseline"
git -C "$root" ls-tree -r 591aafeea2fa6b50086982b05fe0123d4ade3ea6 | \
	awk '$1 == "160000" { print $4 }' | while read -r module; do
	rmdir "$baseline/$module" 2>/dev/null || true
	mkdir -p "$(dirname "$baseline/$module")"
	ln -s "$root/$module" "$baseline/$module"
done
ln -s "$root/../intel_fsp" "$temporary/intel_fsp"

build_off()
{
	tree=$1
	name=$2
	output="$temporary/$name"
	config="$output/full.config"
	mkdir -p "$output"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
	"$tree/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO \
		-d PAYLOAD_SEABIOS -e PAYLOAD_NONE
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		olddefconfig >/dev/null
	if [ "$tree" = "$root" ]; then
		! grep -q '^CONFIG_SMM_PRE_LOCK_DISPATCH=y$' "$config"
		! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_DISPATCH=y$' "$config"
		! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION=y$' "$config"
		! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_LIFECYCLE_MAILBOX_AUTHORITY=y$' "$config"
	fi
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		-j4 "$output/smm/smm" "$output/cbfs/fallback/ramstage.elf" >/dev/null
}
build_off "$root" current-off
build_off "$baseline" base-off
cmp "$temporary/current-off/smm/smm" "$temporary/base-off/smm/smm"
cmp "$temporary/current-off/cbfs/fallback/ramstage.elf" \
	"$temporary/base-off/cbfs/fallback/ramstage.elf"
test ! -e "$temporary/current-off/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_dispatch.o"
test ! -e "$temporary/current-off/smm/mainboard/starlabs/starbook/variants/mtl/dma_smm_authority.o"

echo "StarBook MTL lifecycle-close dispatch profiles: PASS (frame $frame bytes)"
