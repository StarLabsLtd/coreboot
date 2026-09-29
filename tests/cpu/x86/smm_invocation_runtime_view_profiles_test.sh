#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.runtime-view-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=ce93ac31a672054f4bcbf326cb5d45f235a20c78

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
	select SMM_INVOCATION_RUNTIME_BINDING
	select SMM_INVOCATION_AUXILIARY_CHANNELS

config TEST_MTL_RUNTIME_VIEW_STM_PROFILE
	bool "test-only MTL STM invocation runtime view"
	default n
	select TEST_MTL_RUNTIME_VIEW_PROFILE
	select ENABLE_VMX
	select STM
EOF
early_kconfig="$temporary/Kconfig.runtime-view-profile.early"
head -n 3 "$profile_kconfig" > "$early_kconfig"
cat >> "$early_kconfig" <<'EOF'

config SMM_MODULE_STACK_SIZE
	hex
	default 0x2000 if TEST_MTL_RUNTIME_VIEW_PROFILE
EOF
tail -n +4 "$profile_kconfig" >> "$early_kconfig"
mv "$early_kconfig" "$profile_kconfig"

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
	if [ "$name" != Q35_ON ]; then
		grep -q '^CONFIG_SMM_INVOCATION_AUXILIARY_CHANNELS=y$' "$config"
		grep -q '^CONFIG_SMM_INVOCATION_LOADER_READERS=y$' "$config"
	fi
	stack_flags='-fstack-usage -fcallgraph-info=su'
	extra_targets=
	if [ "$name" != Q35_ON ]; then
		extra_targets="$build/ramstage/cpu/x86/smm_invocation_auxiliary_channels.o
$build/smm/cpu/x86/smm_invocation_auxiliary_channels.o
$build/smm/cpu/x86/smm_invocation_topology.o
$build/smm/cpu/x86/smm_invocation_loader_instance.o"
	fi
	# shellcheck disable=SC2086
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		STACK_AUDIT_CFLAGS="$stack_flags" "$build/smm/smm" \
		"$build/ramstage/cpu/x86/smm/smm_module_loader.o" \
		$extra_targets >/dev/null
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
	if [ "$name" != Q35_ON ]; then
		aux_ramstage="$build/ramstage/cpu/x86/smm_invocation_auxiliary_channels.o"
		aux_smm="$build/smm/cpu/x86/smm_invocation_auxiliary_channels.o"
		loader="$build/ramstage/cpu/x86/smm/smm_module_loader.o"
		topology_reader="$build/smm/cpu/x86/smm_invocation_topology.o"
		instance_reader="$build/smm/cpu/x86/smm_invocation_loader_instance.o"
		test -s "$aux_ramstage" && test -s "$aux_smm" && \
			test -s "$topology_reader" && test -s "$instance_reader"
		file "$aux_ramstage" "$aux_smm" "$topology_reader" \
			"$instance_reader" | grep -c 'ELF 32-bit' | grep -q '^4$'
		nm --defined-only "$aux_ramstage" | grep -q \
			'smm_invocation_auxiliary_channels_compose'
		nm --defined-only "$aux_smm" | grep -q \
			'smm_invocation_auxiliary_channel_evidence'
		nm --defined-only "$handler" | grep -q \
			'smm_invocation_runtime_auxiliary_binding_get'
		nm -u "$loader" | grep -q \
			'smm_invocation_auxiliary_channels_compose'
		nm -u "$loader" | grep -q \
			'smm_invocation_auxiliary_channels_loader_abort'
		test "$(objdump -dr "$handler" | sed -n \
			'/<smm_invocation_runtime_auxiliary_binding_get>:/,/^$/p' | \
			grep -c 'smm_invocation_auxiliary_channel_evidence')" -eq 2
		! nm -u "$aux_ramstage" "$aux_smm" "$topology_reader" \
			"$instance_reader" | grep -Eq '__atomic_load_8|libatomic'
		loader_source="$root/src/cpu/x86/smm/smm_module_loader.c"
		primary_line=$(grep -n 'smm_invocation_loader_compose(' \
			"$loader_source" | tail -1 | cut -d: -f1)
		auxiliary_line=$(grep -n 'smm_invocation_auxiliary_channels_compose(' \
			"$loader_source" | cut -d: -f1)
		mailbox_line=$(grep -n 'mailbox_loader_provision(' \
			"$loader_source" | cut -d: -f1)
		fail_line=$(grep -n '^fail:' "$loader_source" | cut -d: -f1)
		abort_line=$(grep -n 'smm_invocation_auxiliary_channels_loader_abort(' \
			"$loader_source" | cut -d: -f1)
		test "$primary_line" -lt "$auxiliary_line"
		test "$auxiliary_line" -lt "$mailbox_line"
		test "$fail_line" -lt "$abort_line"
	fi
	graph="$build/runtime-view.ci"
	find "$build/smm" -type f -name '*.ci' -exec cat {} + > "$graph"
	test -s "$graph"
	stack_bound=$(awk -v limit=512 \
		-f "$root/tests/cpu/x86/smm_invocation_runtime_view_stack_graph.awk" \
		"$graph")
	case "$stack_bound" in
	''|*[!0-9]*) exit 1 ;;
	esac
	if [ "$name" != Q35_ON ]; then
		auxiliary_stack_bound=$(awk -v auxiliary=1 -v limit=8191 \
			-f "$root/tests/cpu/x86/smm_invocation_runtime_view_stack_graph.awk" \
			"$graph")
		case "$auxiliary_stack_bound" in
		''|*[!0-9]*) exit 1 ;;
		esac
		test "$auxiliary_stack_bound" -lt 8192
	fi
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
	if [ "$name" != Q35_ON ]; then
		for edge in \
			'smm_invocation_runtime_auxiliary_binding_get.*runtime_geometry_snapshot' \
			'smm_invocation_runtime_auxiliary_binding_get.*smm_invocation_auxiliary_channel_evidence' \
			'smm_invocation_auxiliary_channel_evidence.*smm_invocation_topology_read' \
			'smm_invocation_auxiliary_channel_evidence.*smm_invocation_loader_instance_read' \
			'smm_invocation_auxiliary_channel_evidence.*smm_invocation_loader_composition_evidence' \
			'smm_invocation_auxiliary_channel_evidence.*evidence_bound' \
			'smm_invocation_auxiliary_channel_evidence.*auxiliary_seed_build' \
			'smm_invocation_auxiliary_channel_evidence.*auxiliary_scrub'; do
			mutant="$build/auxiliary-missing-edge.ci"
			sed "/edge:.*$edge/d" "$graph" > "$mutant"
			! cmp -s "$graph" "$mutant"
			! awk -v auxiliary=1 -v limit=8191 \
				-f "$root/tests/cpu/x86/smm_invocation_runtime_view_stack_graph.awk" \
				"$mutant" >/dev/null 2>&1
		done
		for mutation in recursive indirect unresolved deep; do
			mutant="$build/auxiliary-$mutation.ci"
			cp "$graph" "$mutant"
			case "$mutation" in
			recursive)
				printf '%s\n' 'edge: { sourcename: "smm_invocation_runtime_auxiliary_binding_get" targetname: "smm_invocation_runtime_auxiliary_binding_get" label: "mutation:1:1" }' >> "$mutant" ;;
			indirect)
				printf '%s\n' 'node: { title: "__indirect_call" label: "__indirect_call\\nmutation:1:1" shape: ellipse }' 'edge: { sourcename: "smm_invocation_runtime_auxiliary_binding_get" targetname: "__indirect_call" label: "mutation:1:1" }' >> "$mutant" ;;
			unresolved)
				printf '%s\n' 'node: { title: "missing_auxiliary_target" label: "missing_auxiliary_target\\nmutation:1:1" shape: ellipse }' 'edge: { sourcename: "smm_invocation_runtime_auxiliary_binding_get" targetname: "missing_auxiliary_target" label: "mutation:1:1" }' >> "$mutant" ;;
			deep)
				printf '%s\n' 'node: { title: "auxiliary_deep" label: "auxiliary_deep\\nmutation:1:1\\n8192 bytes (static)" }' 'edge: { sourcename: "smm_invocation_runtime_auxiliary_binding_get" targetname: "auxiliary_deep" label: "mutation:1:1" }' >> "$mutant" ;;
			esac
			! awk -v auxiliary=1 -v limit=8191 \
				-f "$root/tests/cpu/x86/smm_invocation_runtime_view_stack_graph.awk" \
				"$mutant" >/dev/null 2>&1
		done
	fi
done

printf '%s\n' 'SMM invocation runtime view profiles: PASS'
