#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "$root/../.mtl-presence-route-owner.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
baseline="$temporary/base"
vboot_source="$root/3rdparty/vboot"

test -f "$vboot_source/firmware/include/vb2_sha.h"
test -f "$root/3rdparty/stm/Readme.STMPE"

scratch_make()
(
	tree=$1
	shift
	make_vboot_source=$vboot_source
	[ "$tree" = "$baseline" ] && make_vboot_source=3rdparty/vboot
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make BUILD_TIMELESS=1 KERNELVERSION=coreboot-mtl-presence-route-owner \
		VBOOT_SOURCE="$make_vboot_source" -C "$tree" "$@"
)

profile_kconfig="$temporary/Kconfig.route-owner-profile"
head -n 3 "$root/src/Kconfig" > "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PREREQUISITES
	bool "test-only MTL presence route owner prerequisites"
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

config TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PROFILE
	bool "test-only MTL presence route composition owner"
	default n
	select TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PREREQUISITES
	select STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PREREQUISITES
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile_kconfig"

lib_kconfig="$temporary/lib.Kconfig"
sed -e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PREREQUISITES\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" \
	"$profile_kconfig"

build="$temporary/MTL_ON"
config="$build/full.config"
mkdir -p "$build"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile_kconfig" \
	KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
	-e TEST_MTL_AUTHVAR_PRESENCE_ROUTE_OWNER_PROFILE -d LTO
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile_kconfig" olddefconfig >/dev/null
for symbol in STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER \
	PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PLATFORM PAYLOAD_MM_AUTHVAR_PRESENCE_ARM \
	PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION \
	SMM_INVOCATION_INTEL_ADAPTER_ROUTE; do
	grep -q "^CONFIG_${symbol}=y$" "$config"
done
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
	KBUILD_KCONFIG="$profile_kconfig" STACK_AUDIT_CFLAGS=-fstack-usage -j4 \
	"$build/smm/smm" >/dev/null

object="$build/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_route_composition.o"
test -s "$object"
file "$object" | grep -q 'ELF 32-bit'
test "$(nm --defined-only "$object" | awk \
	'$3 == "platform_payload_mm_authvar_presence_arm" { count++ } \
	 END { print count + 0 }')" -eq 1
test "$(nm --defined-only "$object" | awk \
	'$3 == "smm_get_payload_mm_authvar_presence_transaction_slot" { count++ } \
	 END { print count + 0 }')" -eq 1
test "$(nm --defined-only "$object" | awk \
	'$3 == "starbook_mtl_authvar_presence_route_composition_provision" { count++ } \
	 END { print count + 0 }')" -eq 1
nm -u "$object" | awk '{ print $2 }' > "$temporary/undefined"
printf '%s\n' intel_smm_invocation_adapter_route_provision > \
	"$temporary/expected-undefined"
cmp "$temporary/expected-undefined" "$temporary/undefined"
! nm -u "$object" | grep -Eq '__atomic|__sync|libatomic'
test ! -e "$build/ramstage/mainboard/starlabs/starbook/variants/mtl/authvar_presence_route_composition.o"

mkdir -p "$baseline"
git -C "$root" archive 48278012ebadc48028f2bd9d86a79d6f7f3ee5d2 | \
	tar -x -C "$baseline"
rmdir "$baseline/3rdparty/vboot" "$baseline/3rdparty/stm"
ln -s "$vboot_source" "$baseline/3rdparty/vboot"
ln -s "$root/3rdparty/stm" "$baseline/3rdparty/stm"
ln -s "$root/../../intel_fsp" "$temporary/intel_fsp"

build_natural()
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
		! grep -q '^CONFIG_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_ROUTE_COMPOSITION_OWNER=y$' \
			"$config"
	fi
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$output" DOTCONFIG="$config" \
		-j4 "$output/smm/smm" >/dev/null
}

build_natural "$root" current-off
build_natural "$baseline" base-off
cmp "$temporary/current-off/smm/smm" "$temporary/base-off/smm/smm"
test ! -e "$temporary/current-off/smm/mainboard/starlabs/starbook/variants/mtl/authvar_presence_route_composition.o"

echo 'StarBook MTL authenticated-variable presence route composition profiles: PASS'
