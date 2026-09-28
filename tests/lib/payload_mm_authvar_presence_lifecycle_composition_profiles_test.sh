#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "$root/../.lifecycle-installation-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
if [ -z "${LIFECYCLE_INSTALLATION_KEEP_PROFILE:-}" ]; then
	trap 'rm -rf "$temporary"' EXIT HUP INT TERM
else
	printf 'lifecycle-installation profile directory: %s\n' "$temporary" >&2
fi
base=6dbdaf0e671ec1c470782e0c3938cc341b302e23
baseline="$temporary/base"

test -f "$root/3rdparty/vboot/firmware/include/vb2_sha.h"
test -f "$root/3rdparty/stm/Readme.STMPE"

scratch_make()
(
	tree=$1
	shift
	vboot_source="$root/3rdparty/vboot"
	if [ "$tree" = "$baseline" ]; then
		vboot_source=3rdparty/vboot
	fi
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make BUILD_TIMELESS=1 KERNELVERSION=coreboot-lifecycle-installation-test \
		VBOOT_SOURCE="$vboot_source" -C "$tree" "$@"
)

profile_kconfig="$temporary/Kconfig.lifecycle-installation-profile"
head -n 3 "$root/src/Kconfig" > "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES
	bool "test-only MTL lifecycle installation prerequisites"
	default n
	select BOOTMEM_ALIGNED_RESERVATIONS
	select BOOTMEM_ALIGNED_RESERVATION_RECEIPT
	select ENABLE_EARLY_DMA_PROTECTION
	select PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER
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
	select SMM_INVOCATION_INTEL_ADAPTER_ROUTE
	select SMM_INVOCATION_TUPLE_TRIGGER
	select PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER

config TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PROFILE
	bool "test-only MTL authenticated-variable lifecycle installation"
	default n
	select TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES
	select PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_EVIDENCE

config TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_STM_PROFILE
	bool "test-only MTL STM authenticated-variable lifecycle installation"
	default n
	select TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PROFILE
	select ENABLE_VMX
	select STM

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile_kconfig"

lib_kconfig="$temporary/lib.Kconfig"
sed -e 's/depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER$/'\
'depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER || '\
'TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES/' \
	-e 's/depends on PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY$/'\
'depends on PAYLOAD_MM_AUTHVAR_PRESENCE_AUTHORITY || '\
'TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PLATFORM$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ARM$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PREREQUISITES\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" \
	"$profile_kconfig"

configure_on()
{
	name=$1
	selector=$2
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig \
		>/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
		-e "$selector" -d LTO
	scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		olddefconfig >/dev/null
}

configure_on MTL_ON TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_PROFILE
configure_on MTL_STM_ON TEST_MTL_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_STM_PROFILE
for name in MTL_ON MTL_STM_ON; do
	build="$temporary/$name"
	config="$build/full.config"
	grep -q '^CONFIG_ARCH_RAMSTAGE_X86_32=y$' "$config"
	grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION=y$' "$config"
	grep -q '^CONFIG_SMM_INVOCATION_TUPLE_TRIGGER=y$' "$config"
	grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_EVIDENCE=y$' "$config"
	if [ "$name" = MTL_STM_ON ]; then
		grep -q '^CONFIG_STM=y$' "$config"
	else
		! grep -q '^CONFIG_STM=y$' "$config"
	fi
	scratch_make "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		STACK_AUDIT_CFLAGS='-fstack-usage' -j4 \
		"$build/cbfs/fallback/ramstage.debug" "$build/smm/smm" \
		>/dev/null
	for stem in lifecycle_composition lifecycle_registration \
		pre_external_image_registration payload_failure_registration \
		warm_reset_registration s3_resume_registration; do
		object="$build/ramstage/lib/payload_mm_authvar_presence_${stem}.o"
		test -s "$object"
		file "$object" | grep -q 'ELF 32-bit'
		test "$(find "$build" -type f \
			-name "payload_mm_authvar_presence_${stem}.o" | wc -l)" -eq 1
		! nm -u "$object" | grep -Eq '__atomic|libatomic'
		if objdump -d "$object" | grep -Eq \
			'(^|[[:space:]])(out|outs)[bwl]?[[:space:]]'; then
			printf '%s\n' 'lifecycle installation gained an OUT instruction' >&2
			exit 1
		fi
	done
	stack_files=$(find "$build/ramstage/lib" -type f \
		-name '*payload_mm_authvar_presence_*lifecycle*.su' -o \
		-name '*payload_mm_authvar_presence_*registration*.su')
	test -n "$stack_files"
	awk '$2 > 512 { exit 1 }' $stack_files
done

mkdir -p "$baseline"
git -C "$root" archive "$base" | tar -x -C "$baseline"
rmdir "$baseline/3rdparty/vboot" "$baseline/3rdparty/stm"
ln -s "$root/3rdparty/vboot" "$baseline/3rdparty/vboot"
ln -s "$root/3rdparty/stm" "$baseline/3rdparty/stm"
ln -s "$root/../../intel_fsp" "$temporary/intel_fsp"

build_natural()
(
	tree=$1
	name=$2
	profile=$3
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_DEFCONFIG="configs/config.$profile" \
		defconfig >/dev/null
	"$tree/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" olddefconfig >/dev/null
	if [ "$tree" = "$root" ]; then
		! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_INSTALLATION_EVIDENCE=y$' \
			"$config"
	fi
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" -j4 "$build/cbfs/fallback/ramstage.debug" \
		"$build/smm/smm" >/dev/null
	for stem in lifecycle_composition lifecycle_registration \
		pre_external_image_registration payload_failure_registration \
		warm_reset_registration s3_resume_registration; do
		test ! -e \
			"$build/ramstage/lib/payload_mm_authvar_presence_${stem}.o"
	done
)

for profile in emulation_qemu_x86_q35_smm_tseg starlabs_lite_glk \
	starlabs_lite_adl starlabs_starbook_mtl; do
	name=$(printf '%s' "$profile" | tr / _)
	build_natural "$root" "current-$name" "$profile"
	build_natural "$baseline" "base-$name" "$profile"
	objcopy -O binary "$temporary/current-$name/cbfs/fallback/ramstage.debug" \
		"$temporary/current-$name.ramstage"
	objcopy -O binary "$temporary/base-$name/cbfs/fallback/ramstage.debug" \
		"$temporary/base-$name.ramstage"
	cmp "$temporary/current-$name.ramstage" \
		"$temporary/base-$name.ramstage"
	cmp "$temporary/current-$name/smm/smm" \
		"$temporary/base-$name/smm/smm"
done

printf '%s\n' 'Payload-MM authenticated-variable lifecycle installation profiles: PASS'
