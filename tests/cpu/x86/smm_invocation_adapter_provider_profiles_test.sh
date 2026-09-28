#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.adapter-provider-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
vboot_source="$root/3rdparty/vboot"
if [ ! -f "$vboot_source/firmware/include/vb2_sha.h" ]; then
	vboot_source="$root/../../coreboot/3rdparty/vboot"
fi
test -f "$vboot_source/firmware/include/vb2_sha.h"

scratch_make()
(
	make_vboot_source=$vboot_source
	case " $* " in
	*" -C ${baseline:-/nonexistent} "*) make_vboot_source=3rdparty/vboot ;;
	esac
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make VBOOT_SOURCE="$make_vboot_source" "$@"
)

profile_kconfig="$temporary/Kconfig.adapter-provider-profile"
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_Q35_ADAPTER_PROVIDER_OFF_PROFILE
	bool "test-only Q35 provider-off SMM"
	default n
	select Q35_SMM_INVOCATION_FAIL_STOP_TEST
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select SMM_INVOCATION_LOADER_INSTANCE_PLATFORM
	select SMM_INVOCATION_LOADER_COMPOSITION
	select SMM_INVOCATION_RUNTIME_VIEW

config TEST_MTL_ADAPTER_PROVIDER_PROFILE
	bool "test-only MTL adapter provider"
	default n
	select ENABLE_EARLY_DMA_PROTECTION
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

config TEST_MTL_ADAPTER_PROVIDER_STM_PROFILE
	bool "test-only MTL STM adapter provider"
	default n
	select TEST_MTL_ADAPTER_PROVIDER_PROFILE
	select ENABLE_VMX
	select STM
EOF

configure()
{
	name=$1
	defconfig=$2
	selector=$3
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG="configs/config.$defconfig" defconfig >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	[ -z "$selector" ] || \
		"$root/util/scripts/config" --file "$config" -e "$selector"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		olddefconfig >/dev/null
}

configure Q35_OFF emulation_qemu_x86_q35_smm_tseg \
	TEST_Q35_ADAPTER_PROVIDER_OFF_PROFILE
configure GLK_OFF starlabs_lite_glk ''
configure ADL_OFF starlabs_lite_adl ''
configure MTL_OFF starlabs_starbook_mtl ''
for name in Q35_OFF GLK_OFF ADL_OFF MTL_OFF; do
	config="$temporary/$name/full.config"
	! grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER_PROVIDER=y$' "$config"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/$name" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$temporary/$name/smm/smm" >/dev/null
	! find "$temporary/$name" -type f -name 'invocation_adapter_provider.o' | grep -q .
done

configure MTL_ON starlabs_starbook_mtl TEST_MTL_ADAPTER_PROVIDER_PROFILE
configure MTL_STM_ON starlabs_starbook_mtl \
	TEST_MTL_ADAPTER_PROVIDER_STM_PROFILE
for name in MTL_ON MTL_STM_ON; do
	build="$temporary/$name"
	config="$build/full.config"
	grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER_PROVIDER=y$' "$config"
	grep -q '^CONFIG_SMM_INVOCATION_RUNTIME_VIEW=y$' "$config"
	if [ "$name" = MTL_STM_ON ]; then
		grep -q '^CONFIG_STM=y$' "$config"
	else
		! grep -q '^CONFIG_STM=y$' "$config"
	fi
	stack_flags='-fstack-usage -fcallgraph-info=su'
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		STACK_AUDIT_CFLAGS="$stack_flags" \
		"$build/smm/smm" >/dev/null
	object="$build/smm/soc/intel/common/block/smm/invocation_adapter_provider.o"
	test -f "$object"
	for symbol in intel_smm_invocation_adapter_provider_provision \
		intel_smm_invocation_adapter_provider_arm \
		intel_smm_invocation_adapter_provider_retire; do
		test "$(nm --defined-only "$object" | awk -v symbol="$symbol" \
			'$3 == symbol { count++ } END { print count + 0 }')" -eq 1
		test "$(find "$build/smm" -type f -name '*.o' ! -path "*$object" \
			-exec nm --defined-only {} + 2>/dev/null | awk -v symbol="$symbol" \
			'$NF == symbol { count++ } END { print count + 0 }')" -eq 0
		find "$build" -path "$build/smm" -prune -o -type f -name '*.o' \
			-exec nm --defined-only {} + 2>/dev/null | \
			awk -v symbol="$symbol" '$NF == symbol { found = 1 } END { exit found }'
	done
	! nm -u "$object" | grep -Eq '__atomic_|__sync_'
	graph="$build/adapter-provider.ci"
	find "$build/smm" -type f -name '*.ci' -exec cat {} + > "$graph"
	test -s "$graph"
	stack_bound=$(awk -v limit=1024 \
		-f "$root/tests/cpu/x86/smm_invocation_adapter_provider_stack_graph.awk" \
		"$graph")
	case "$stack_bound" in ''|*[!0-9]*) exit 1 ;; esac
	for mutation in collision dynamic missing nested recursive indirect unknown; do
		mutant="$build/adapter-provider-$mutation.ci"
		cp "$graph" "$mutant"
		case "$mutation" in
		collision)
			printf '%s\n' \
				'node: { title: "revision_size" label: "revision_size" shape: ellipse }' \
				'edge: { sourcename: "intel_smm_invocation_adapter_bind" targetname: "revision_size" label: "mutation:1:1" }' >> "$mutant" ;;
		dynamic)
			printf '%s\n' \
				'node: { title: "provider_dynamic" label: "provider_dynamic\\n8 bytes (dynamic)" shape: ellipse }' \
				'node: { title: "provider_dynamic.constprop.0" label: "provider_dynamic.constprop.0\\n8 bytes (static)" shape: ellipse }' \
				'edge: { sourcename: "intel_smm_invocation_adapter_bind" targetname: "provider_dynamic" label: "mutation:1:1" }' >> "$mutant" ;;
		missing)
			sed '/edge:.*intel_smm_invocation_adapter_provider_retire.*intel_smm_invocation_adapter_end/d' \
				"$graph" > "$mutant" ;;
		nested)
			printf '%s\n' \
				'node: { title: "provider_nested_1" label: "provider_nested_1\\n400 bytes (static)" shape: ellipse }' \
				'node: { title: "provider_nested_2" label: "provider_nested_2\\n400 bytes (static)" shape: ellipse }' \
				'node: { title: "provider_nested_3" label: "provider_nested_3\\n400 bytes (static)" shape: ellipse }' \
				'edge: { sourcename: "intel_smm_invocation_adapter_bind" targetname: "provider_nested_1" label: "mutation:1:1" }' \
				'edge: { sourcename: "provider_nested_1" targetname: "provider_nested_2" label: "mutation:1:1" }' \
				'edge: { sourcename: "provider_nested_2" targetname: "provider_nested_3" label: "mutation:1:1" }' >> "$mutant" ;;
		recursive)
			printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_bind" targetname: "intel_smm_invocation_adapter_bind" label: "mutation:1:1" }' >> "$mutant" ;;
		indirect)
			printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_bind" targetname: "__indirect_call" label: "mutation:1:1" }' >> "$mutant" ;;
		unknown)
			printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_bind" targetname: "missing_provider_target" label: "mutation:1:1" }' >> "$mutant" ;;
		esac
		! awk -v limit=1024 \
			-f "$root/tests/cpu/x86/smm_invocation_adapter_provider_stack_graph.awk" \
			"$mutant" >/dev/null 2>&1
	done
done

baseline="$temporary/base"
mkdir -p "$baseline"
git -C "$root" archive 6e187a842ef7ca7d6c7a955cb15765a3b91e2450 | \
	tar -x -C "$baseline"
rmdir "$baseline/3rdparty/vboot" "$baseline/3rdparty/stm"
ln -s "$vboot_source" "$baseline/3rdparty/vboot"
ln -s "$root/../../coreboot/3rdparty/stm" "$baseline/3rdparty/stm"
ln -s "$root/../../intel_fsp" "$temporary/intel_fsp"

compare_base()
{
	name=$1
	defconfig=$2
	selector=$3
	base_build="$temporary/BASE_$name"
	base_config="$base_build/full.config"
	mkdir -p "$base_build"
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$base_build" \
		DOTCONFIG="$base_config" KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG="configs/config.$defconfig" defconfig >/dev/null
	"$baseline/util/scripts/config" --file "$base_config" -e ANY_TOOLCHAIN
	[ -z "$selector" ] || \
		"$baseline/util/scripts/config" --file "$base_config" -e "$selector"
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$base_build" \
		DOTCONFIG="$base_config" KBUILD_KCONFIG="$profile_kconfig" \
		olddefconfig >/dev/null
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$base_build" \
		DOTCONFIG="$base_config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$base_build/smm/smm" >/dev/null
	cmp "$temporary/$name/smm/smm" "$base_build/smm/smm"
	for object in smm/cpu/x86/smm/smm_module_handler.o \
		smm/soc/intel/common/block/smm/invocation_adapter.o; do
		current="$temporary/$name/$object"
		base_object="$base_build/$object"
		if [ -f "$current" ] || [ -f "$base_object" ]; then
			test -f "$current" && test -f "$base_object"
			objcopy --strip-debug "$current" "$temporary/current.o"
			objcopy --strip-debug "$base_object" "$temporary/base.o"
			cmp "$temporary/current.o" "$temporary/base.o"
		fi
	done
}

compare_base Q35_OFF emulation_qemu_x86_q35_smm_tseg \
	TEST_Q35_ADAPTER_PROVIDER_OFF_PROFILE
compare_base GLK_OFF starlabs_lite_glk ''
compare_base ADL_OFF starlabs_lite_adl ''
compare_base MTL_OFF starlabs_starbook_mtl ''

printf '%s\n' 'Intel SMM invocation adapter provider profiles: PASS'
