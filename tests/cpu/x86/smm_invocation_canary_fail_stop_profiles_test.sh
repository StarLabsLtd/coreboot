#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.canary-fail-stop-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=3e78227ef0f9f26ccb52f44b0903e4c6b7fd6bdd

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make "$@"
)

profile_kconfig="$temporary/Kconfig.canary-fail-stop-profile"
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_Q35_CANARY_FAIL_STOP_PROFILE
	bool "test-only Q35 canary fail-stop profile"
	default n
	select Q35_SMM_INVOCATION_FAIL_STOP_TEST
	select SMM_INVOCATION_STACK_CANARY_FAIL_STOP

config TEST_MTL_CANARY_FAIL_STOP_PROFILE
	bool "test-only MTL canary fail-stop profile"
	default n
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP
	select SMM_INVOCATION_STACK_CANARY_FAIL_STOP
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
	! grep -q '^CONFIG_SMM_INVOCATION_STACK_CANARY_FAIL_STOP=y$' "$config"
	if ! scratch_make -C "$root" UPDATED_SUBMODULES=1 V=1 obj="$temporary/$name" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$temporary/$name/smm/smm" >"$temporary/$name/build.log" 2>&1; then
		cat "$temporary/$name/build.log" >&2
		exit 1
	fi
done

# Compare both default linked SMM images and handler objects against PR259.
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
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$base_build" \
		DOTCONFIG="$base_config" -j4 "$base_build/smm/smm" >/dev/null
	if ! cmp "$temporary/$name/smm/smm" "$base_build/smm/smm"; then
		printf '%s\n' "$name default linked SMM changed" >&2
		exit 1
	fi
	objcopy --strip-debug \
		"$temporary/$name/smm/cpu/x86/smm/smm_module_handler.o" \
		"$temporary/$name/handler.stripped.o"
	objcopy --strip-debug \
		"$base_build/smm/cpu/x86/smm/smm_module_handler.o" \
		"$base_build/handler.stripped.o"
	if ! cmp "$temporary/$name/handler.stripped.o" \
		"$base_build/handler.stripped.o"; then
		printf '%s\n' "$name default handler object changed" >&2
		exit 1
	fi
}

compare_base Q35_OFF emulation_qemu_x86_q35_smm_tseg
compare_base MTL_OFF starlabs_starbook_mtl

configure Q35_ON emulation_qemu_x86_q35_smm_tseg \
	TEST_Q35_CANARY_FAIL_STOP_PROFILE
configure MTL_ON starlabs_starbook_mtl TEST_MTL_CANARY_FAIL_STOP_PROFILE
for name in Q35_ON MTL_ON; do
	build="$temporary/$name"
	config="$build/full.config"
	grep -q '^CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM=y$' "$config"
	grep -q '^CONFIG_SMM_INVOCATION_STACK_CANARY_FAIL_STOP=y$' "$config"
	stack_flags='-fstack-usage -fcallgraph-info=su -save-temps=obj'
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		STACK_AUDIT_CFLAGS="$stack_flags" \
		"$build/smm/smm" >/dev/null
	handler="$build/smm/cpu/x86/smm/smm_module_handler.o"
	case "$name" in
	Q35_ON)
		provider="$build/smm/mainboard/emulation/qemu-q35/smm_invocation_fail_stop.o"
		;;
	MTL_ON)
		provider="$build/smm/mainboard/starlabs/starbook/variants/mtl/smm_invocation_fail_stop.o"
		;;
	esac
	test -s "$provider"
	test "$(nm -u "$handler" | awk \
		'$2 == "smm_invocation_platform_fail_stop" { count++ } END { print count + 0 }')" \
		-eq 1
	test "$(nm --defined-only "$build/smm/smm.elf" | awk \
		'$3 == "smm_invocation_platform_fail_stop" { count++ } END { print count + 0 }')" \
		-eq 1
	test "$(objdump -dr "$handler" | \
		grep -c 'smm_invocation_platform_fail_stop')" -eq 1
	! objdump -dr "$provider" | grep -Eq '[[:space:]]call.*\*'
	! nm -u "$handler" | grep -q '__atomic_'
	# Cache cleaning and halt are architecture inline wrappers. Their work is
	# charged to the compiler-reported caller frame; pin the source-level
	# composition while the graph below proves the external abort/reset/TCO
	# edges and every further external descendant.
	grep -q 'dcache_clean_all();' "$root/src/arch/x86/cf9_reset.c"
	grep -q 'abort();' "$root/src/include/halt.h"
	grep -q 'tcobase = tco_get_bar();' \
		"$root/src/soc/intel/common/block/smbus/tco.c"
	graph="$build/canary-fail-stop.ci"
	find "$build/smm" -type f -name '*.ci' -exec cat {} + > "$graph"
	test -s "$graph"
	stack_size=$(awk '$1 == "#define" && $2 == "CONFIG_SMM_MODULE_STACK_SIZE" {
		print $3; found++
	} END { if (found != 1) exit 1 }' "$build/config.h")
	stack_reserve=512
	graph_args="-v limit=$((stack_size - stack_reserve))"
	if [ "$name" = MTL_ON ]; then
		graph_args="$graph_args -v mtl=1"
	fi
	stack_bound=$(awk $graph_args \
		-f "$root/tests/cpu/x86/smm_invocation_canary_fail_stop_stack_graph.awk" \
		"$graph")
	case "$stack_bound" in
	''|*[!0-9]*) exit 1 ;;
	esac
	missing_edge="$build/canary-fail-stop-missing-edge.ci"
	sed '/smm_invocation_platform_fail_stop.*do_system_reset/d' \
		"$graph" > "$missing_edge"
	! cmp -s "$graph" "$missing_edge"
	! awk $graph_args \
		-f "$root/tests/cpu/x86/smm_invocation_canary_fail_stop_stack_graph.awk" \
		"$missing_edge" >/dev/null 2>&1
	missing_entry="$build/canary-fail-stop-missing-entry.ci"
	sed '/smm_handler_start.*smm_invocation_platform_fail_stop/d' \
		"$graph" > "$missing_entry"
	! cmp -s "$graph" "$missing_entry"
	! awk $graph_args \
		-f "$root/tests/cpu/x86/smm_invocation_canary_fail_stop_stack_graph.awk" \
		"$missing_entry" >/dev/null 2>&1
	provider_node=$(awk '/^node: / &&
		/label: "smm_invocation_platform_fail_stop\\n/ && / bytes / {
			node = $0
			sub(/^node: \{ title: "/, "", node)
			sub(/".*/, "", node)
			print node
			found++
		} END { if (found != 1) exit 1 }' "$graph")
	for mutation in indirect recursive unknown; do
		mutant="$build/canary-fail-stop-$mutation.ci"
		cp "$graph" "$mutant"
		case "$mutation" in
		indirect)
			printf '%s\n' "edge: { sourcename: \"$provider_node\" targetname: \"__indirect_call\" label: \"mutation:1:1\" }" >> "$mutant"
			;;
		recursive)
			printf '%s\n' "edge: { sourcename: \"$provider_node\" targetname: \"$provider_node\" label: \"mutation:1:1\" }" >> "$mutant"
			;;
		unknown)
			printf '%s\n' \
				'node: { title: "canary_fail_stop_unknown" label: "canary_fail_stop_unknown\nmutation:1:1" }' \
				"edge: { sourcename: \"$provider_node\" targetname: \"canary_fail_stop_unknown\" label: \"mutation:1:1\" }" >> "$mutant"
			;;
		esac
		! awk $graph_args \
			-f "$root/tests/cpu/x86/smm_invocation_canary_fail_stop_stack_graph.awk" \
			"$mutant" >/dev/null 2>&1
	done
	printf '%s\n' \
		"$name rooted fail-stop stack: $stack_bound + $stack_reserve reserve / $stack_size bytes"
done

printf '%s\n' 'SMM invocation stack-canary fail-stop profiles: PASS'
