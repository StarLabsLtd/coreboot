#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.runtime-view-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=f1238b176479acb1547088fa84ccd39cce699c32

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make "$@"
)

profile_kconfig="$temporary/Kconfig.runtime-view-profile"
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_Q35_RUNTIME_VIEW_PROFILE
	bool "test-only Q35 invocation runtime view"
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

config TEST_MTL_RUNTIME_VIEW_PROFILE
	bool "test-only MTL invocation runtime view"
	default n
	select ENABLE_EARLY_DMA_PROTECTION
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER
	select SMM_INVOCATION_LOADER_COMPOSITION
	select SMM_INVOCATION_RUNTIME_VIEW

config TEST_MTL_RUNTIME_VIEW_STM_PROFILE
	bool "test-only MTL STM invocation runtime view"
	default n
	select TEST_MTL_RUNTIME_VIEW_PROFILE
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
	if [ -n "$selector" ]; then
		"$root/util/scripts/config" --file "$config" -e "$selector"
	fi
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		olddefconfig >/dev/null
}

configure Q35_OFF emulation_qemu_x86_q35_smm_tseg ''
configure MTL_OFF starlabs_starbook_mtl ''
for name in Q35_OFF MTL_OFF; do
	config="$temporary/$name/full.config"
	! grep -q '^CONFIG_SMM_INVOCATION_RUNTIME_VIEW=y$' "$config"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/$name" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$temporary/$name/smm/smm" \
		"$temporary/$name/ramstage/cpu/x86/smm/smm_module_loader.o" \
		>/dev/null
done

baseline="$temporary/base"
mkdir -p "$baseline"
git -C "$root" archive "$base" | tar -x -C "$baseline"
ln -s "$root/../intel_fsp" "$temporary/intel_fsp"
for submodule in vboot stm; do
	rmdir "$baseline/3rdparty/$submodule"
	ln -s "$root/3rdparty/$submodule" "$baseline/3rdparty/$submodule"
done

compare_base()
{
	name=$1
	defconfig=$2
	base_build="$temporary/BASE_$name"
	base_config="$base_build/full.config"
	mkdir -p "$base_build"
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$base_build" \
		DOTCONFIG="$base_config" KBUILD_DEFCONFIG="configs/config.$defconfig" \
		defconfig >/dev/null
	"$baseline/util/scripts/config" --file "$base_config" -e ANY_TOOLCHAIN
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$base_build" \
		DOTCONFIG="$base_config" olddefconfig >/dev/null
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$base_build" -j4 \
		DOTCONFIG="$base_config" "$base_build/smm/smm" \
		"$base_build/ramstage/cpu/x86/smm/smm_module_loader.o" >/dev/null
	cmp "$temporary/$name/smm/smm" "$base_build/smm/smm"
	for object in smm/cpu/x86/smm/smm_module_handler.o \
		ramstage/cpu/x86/smm/smm_module_loader.o; do
		current="$temporary/$name/${object##*/}.current"
		baseline_object="$base_build/${object##*/}.base"
		objcopy --strip-debug "$temporary/$name/$object" "$current"
		objcopy --strip-debug "$base_build/$object" "$baseline_object"
		cmp "$current" "$baseline_object"
	done
}

compare_base Q35_OFF emulation_qemu_x86_q35_smm_tseg
compare_base MTL_OFF starlabs_starbook_mtl

configure Q35_ON emulation_qemu_x86_q35_smm_tseg TEST_Q35_RUNTIME_VIEW_PROFILE
configure MTL_ON starlabs_starbook_mtl TEST_MTL_RUNTIME_VIEW_PROFILE
configure MTL_STM_ON starlabs_starbook_mtl TEST_MTL_RUNTIME_VIEW_STM_PROFILE
for name in Q35_ON MTL_ON MTL_STM_ON; do
	build="$temporary/$name"
	config="$build/full.config"
	grep -q '^CONFIG_SMM_INVOCATION_RUNTIME_VIEW=y$' "$config"
	grep -q '^CONFIG_SMM_SAVE_STATE_GEOMETRY=y$' "$config"
	if [ "$name" = MTL_STM_ON ]; then
		grep -q '^CONFIG_STM=y$' "$config"
	else
		! grep -q '^CONFIG_STM=y$' "$config"
	fi
	stack_flags='-fstack-usage -fcallgraph-info=su'
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		STACK_AUDIT_CFLAGS="$stack_flags" "$build/smm/smm" \
		"$build/ramstage/cpu/x86/smm/smm_module_loader.o" >/dev/null
	handler="$build/smm/cpu/x86/smm/smm_module_handler.o"
	for symbol in smm_invocation_runtime_view_get \
		smm_invocation_runtime_cpu_count \
		smm_invocation_runtime_save_state_span \
		smm_invocation_runtime_range_is_protected; do
		test "$(nm --defined-only "$handler" | awk -v symbol="$symbol" \
			'$3 == symbol { count++ } END { print count + 0 }')" -eq 1
		test "$(nm --defined-only "$build/smm/smm.elf" | awk -v symbol="$symbol" \
			'$3 == symbol { count++ } END { print count + 0 }')" -eq 0
	done
	! find "$build" -path '*/smm/*' -prune -o -type f -name '*.o' -print | \
		xargs -r nm --defined-only 2>/dev/null | \
		grep -Eq 'smm_invocation_runtime_(view_get|cpu_count|save_state_span|range_is_protected)'
	! nm -u "$handler" | grep -q '__atomic_'
	graph="$build/runtime-view.ci"
	find "$build/smm" -type f -name '*.ci' -exec cat {} + > "$graph"
	test -s "$graph"
	stack_bound=$(awk -v limit=512 \
		-f "$root/tests/cpu/x86/smm_invocation_runtime_view_stack_graph.awk" \
		"$graph")
	case "$stack_bound" in
	''|*[!0-9]*) exit 1 ;;
	esac
	for edge in \
		'smm_invocation_runtime_view_get.*runtime_geometry_snapshot' \
		'smm_invocation_runtime_cpu_count.*runtime_geometry_snapshot' \
		'smm_invocation_runtime_save_state_span.*runtime_geometry_snapshot' \
		'smm_invocation_runtime_range_is_protected.*runtime_geometry_snapshot' \
		'runtime_geometry_snapshot.*runtime_topology_matches' \
		'runtime_geometry_snapshot.*runtime_composition_matches' \
		'runtime_geometry_snapshot.*runtime_geometry_valid' \
		'runtime_geometry_valid.*smm_save_state_native_span'; do
		mutant="$build/runtime-view-missing-edge.ci"
		sed "/edge:.*$edge/d" "$graph" > "$mutant"
		! cmp -s "$graph" "$mutant"
		! awk -v limit=512 \
			-f "$root/tests/cpu/x86/smm_invocation_runtime_view_stack_graph.awk" \
			"$mutant" >/dev/null 2>&1
	done
	for mutation in recursive indirect unresolved deep; do
		mutant="$build/runtime-view-$mutation.ci"
		cp "$graph" "$mutant"
		case "$mutation" in
		recursive)
			printf '%s\n' 'edge: { sourcename: "smm_invocation_runtime_view_get" targetname: "smm_invocation_runtime_view_get" label: "mutation:1:1" }' >> "$mutant" ;;
		indirect)
			printf '%s\n' 'node: { title: "__indirect_call" label: "__indirect_call\\nmutation:1:1" shape: ellipse }' 'edge: { sourcename: "smm_invocation_runtime_view_get" targetname: "__indirect_call" label: "mutation:1:1" }' >> "$mutant" ;;
		unresolved)
			printf '%s\n' 'node: { title: "missing_runtime_view_target" label: "missing_runtime_view_target\\nmutation:1:1" shape: ellipse }' 'edge: { sourcename: "smm_invocation_runtime_view_get" targetname: "missing_runtime_view_target" label: "mutation:1:1" }' >> "$mutant" ;;
		deep)
			printf '%s\n' 'node: { title: "runtime_view_deep" label: "runtime_view_deep\\nmutation:1:1\\n513 bytes (static)" }' 'edge: { sourcename: "smm_invocation_runtime_view_get" targetname: "runtime_view_deep" label: "mutation:1:1" }' >> "$mutant" ;;
		esac
		! awk -v limit=512 \
			-f "$root/tests/cpu/x86/smm_invocation_runtime_view_stack_graph.awk" \
			"$mutant" >/dev/null 2>&1
	done
done

printf '%s\n' 'SMM invocation runtime view profiles: PASS'
