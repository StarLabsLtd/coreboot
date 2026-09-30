#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "$root/../.mtl-lifecycle-close-install.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
baseline="$temporary/base"

scratch_make()
(
	tree=$1
	shift
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make BUILD_TIMELESS=1 KERNELVERSION=coreboot-mtl-close-install \
		-C "$tree" "$@"
)

profile="$temporary/Kconfig"
head -n 3 "$root/src/Kconfig" > "$profile"
cat >> "$profile" <<'EOF'

config TEST_MTL_LIFECYCLE_CLOSE_INSTALL
	bool "test-only MTL lifecycle-close install owner"
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
	select SMM_APMC_COMPOSITION_ATTESTED
	select SMM_APMC_COMMAND_REGISTRY
	select STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER
	select STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_MTL_LIFECYCLE_CLOSE_INSTALL
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile"

lib_kconfig="$temporary/lib.Kconfig"
sed -e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 s/^\tdefault n$/\tdefault y if TEST_MTL_LIFECYCLE_CLOSE_INSTALL\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 s/^\tdefault n$/\tdefault y if TEST_MTL_LIFECYCLE_CLOSE_INSTALL\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" "$profile"

build="$temporary/on"
config="$build/full.config"
mkdir -p "$build"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl \
	defconfig >/dev/null
"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
	-e TEST_MTL_LIFECYCLE_CLOSE_INSTALL -d LTO
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" olddefconfig >/dev/null
for symbol in STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL \
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT_PROVIDER \
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT \
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT \
	SMM_INVOCATION_RUNTIME_BINDING; do
	grep -q "^CONFIG_${symbol}=y$" "$config"
done
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile" -j4 "$build/smm/smm" \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.o" \
	>/dev/null

receiver="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.o"
sender="$build/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.o"
test -s "$receiver" && test -s "$sender"
file "$receiver" "$sender" | grep -c 'ELF 32-bit' | grep -q '^2$'
test "$(nm --defined-only "$receiver" | awk '$3 == "starbook_mtl_authvar_presence_lifecycle_close_install_receive" { n++ } END { print n + 0 }')" -eq 1
test "$(nm --defined-only "$sender" | awk '$3 == "platform_payload_mm_authvar_presence_lifecycle_close_route_install" { n++ } END { print n + 0 }')" -eq 1
nm -u "$receiver" | grep -q 'payload_mm_authvar_presence_lifecycle_close_route_provision'
nm -u "$receiver" | grep -q 'smm_invocation_runtime_binding_get'
! nm -u "$receiver" | grep -q 'intel_smm_invocation_adapter_provider_'
! nm -u "$receiver" | grep -q 'smm_runtime'
test ! -e "$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.o"
test ! -e "$build/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_receiver.o"

mkdir -p "$baseline"
git -C "$root" archive 597c97e5be6 | tar -x -C "$baseline"
for module in vboot stm; do
	rmdir "$baseline/3rdparty/$module"
	ln -s "$root/3rdparty/$module" "$baseline/3rdparty/$module"
done
ln -s "$root/../../intel_fsp" "$temporary/intel_fsp"

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
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		-j4 "$output/smm/smm" >/dev/null
}
build_off "$root" current-off
build_off "$baseline" base-off
cmp "$temporary/current-off/smm/smm" "$temporary/base-off/smm/smm"
test ! -e "$temporary/current-off/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_sender.o"

echo 'StarBook MTL lifecycle-close installation profiles: PASS'
