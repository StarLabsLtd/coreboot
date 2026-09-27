#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=fd9e9443121bf3927248ca85544f0a9a1bd48290

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES
	exec make "$@"
)

profile_kconfig="$temporary/Kconfig"
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_INVOCATION_COMPOSITION_PROFILE
	bool "test-only generic invocation composition"
	default n
	select SMM_INVOCATION_FAIL_STOP_PLATFORM
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select SMM_INVOCATION_LOADER_INSTANCE_PLATFORM
	select SMM_INVOCATION_LOADER_COMPOSITION

config TEST_MTL_INVOCATION_COMPOSITION_PROFILE
	bool "test-only MTL invocation composition"
	default n
	select ENABLE_EARLY_DMA_PROTECTION
	select SMM_INVOCATION_FAIL_STOP_PLATFORM
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_INTEL_ADAPTER
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER
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

configure OFF starlabs_lite_glk ''
configure Q35_OFF emulation_qemu_x86_q35_smm_tseg ''
configure MTL_OFF starlabs_starbook_mtl ''
off_build="$temporary/OFF"
off_config="$off_build/full.config"
for name in OFF Q35_OFF MTL_OFF; do
	for symbol in SMM_INVOCATION_LOADER_COMPOSITION \
		SMM_INVOCATION_LOADER_INSTANCE_PLATFORM \
		STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER; do
		if grep -q "^CONFIG_${symbol}=y$" "$temporary/$name/full.config"; then
			printf 'composition symbol enabled by default: %s/%s\n' \
				"$name" "$symbol" >&2
			exit 1
		fi
	done
done

off_log="$temporary/config-off.log"
scratch_make -C "$root" UPDATED_SUBMODULES=1 V=1 obj="$off_build" \
	DOTCONFIG="$off_config" KBUILD_KCONFIG="$profile_kconfig" -B \
	"$off_build/ramstage/cpu/x86/smm/smm_module_loader.o" \
	"$off_build/smm/cpu/x86/smm/smm_module_handler.o" >"$off_log" 2>&1

compare_base_object()
{
	source=$1
	current=$2
	name=$3
	command=$(grep "$(basename "$current").*$(basename "$source")" \
		"$off_log" | tail -1)
	test -n "$command"
	base_source="$temporary/$name.base.c"
	base_object="$temporary/$name.base.o"
	git -C "$root" show "$base:$source" > "$base_source"
	base_command=$(printf '%s\n' "$command" | sed \
		"s|-o $current|-o $base_object|; s|$source[[:space:]]*$|$base_source|")
	(cd "$root" && eval "$base_command")
	objcopy --strip-debug "$current" "$temporary/$name.current.stripped.o"
	objcopy --strip-debug "$base_object" "$temporary/$name.base.stripped.o"
	cmp "$temporary/$name.current.stripped.o" \
		"$temporary/$name.base.stripped.o"
}

compare_base_object src/cpu/x86/smm/smm_module_loader.c \
	"$off_build/ramstage/cpu/x86/smm/smm_module_loader.o" loader
compare_base_object src/cpu/x86/smm/smm_module_handler.c \
	"$off_build/smm/cpu/x86/smm/smm_module_handler.o" handler

configure GLK starlabs_lite_glk TEST_INVOCATION_COMPOSITION_PROFILE
configure ADL starlabs_lite_adl TEST_INVOCATION_COMPOSITION_PROFILE
configure Q35 emulation_qemu_x86_q35_smm_tseg \
	TEST_INVOCATION_COMPOSITION_PROFILE
configure MTL starlabs_starbook_mtl \
	TEST_MTL_INVOCATION_COMPOSITION_PROFILE

for name in GLK ADL Q35 MTL; do
	build="$temporary/$name"
	config="$build/full.config"
	for symbol in SMM_INVOCATION_EVIDENCE SMM_INVOCATION_TOPOLOGY \
		SMM_INVOCATION_LOADER_INSTANCE \
		SMM_INVOCATION_LOADER_INSTANCE_PLATFORM \
		SMM_INVOCATION_LOADER_COMPOSITION; do
		grep -q "^CONFIG_${symbol}=y$" "$config"
	done
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -B -j4 \
		"$build/ramstage/cpu/x86/smm/smm_module_loader.o" \
		"$build/ramstage/cpu/x86/smm_invocation_topology.o" \
		"$build/ramstage/cpu/x86/smm_invocation_loader_instance.o" \
		"$build/ramstage/cpu/x86/smm_invocation_evidence_loader.o" \
		"$build/ramstage/cpu/x86/smm_invocation_loader_composition.o" \
		"$build/smm/cpu/x86/smm_invocation_entry.o" \
		"$build/smm/cpu/x86/smm_invocation_evidence.o" \
		"$build/smm/cpu/x86/smm_invocation_evidence_loader.o" \
		"$build/smm/cpu/x86/smm_invocation_loader_composition_gate.o" \
		"$build/smm/cpu/x86/smm/smm_module_handler.o" \
		"$build/smmstub/cpu/x86/smm/smm_stub.o" \
		>/dev/null
	if [ "$name" = MTL ]; then
		scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
			DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -B \
			"$build/smm/soc/intel/common/block/smm/invocation_adapter.o" \
			>/dev/null
	fi
	for object in \
		"$build/ramstage/cpu/x86/smm_invocation_topology.o" \
		"$build/ramstage/cpu/x86/smm_invocation_loader_instance.o" \
		"$build/ramstage/cpu/x86/smm_invocation_evidence_loader.o" \
		"$build/ramstage/cpu/x86/smm_invocation_loader_composition.o"; do
		if nm -u "$object" | grep -q '__atomic_'; then
			printf '%s gained a libatomic dependency\n' "$object" >&2
			exit 1
		fi
	done
	test "$(nm --defined-only \
		"$build/smm/cpu/x86/smm_invocation_evidence_loader.o" | awk \
		'$3 == "smm_invocation_evidence_provision" { count++ } END { print count + 0 }')" -eq 1
	test "$(nm --defined-only \
		"$build/smm/cpu/x86/smm_invocation_loader_composition_gate.o" | awk \
		'$3 == "smm_invocation_loader_composition_evidence" { count++ } END { print count + 0 }')" -eq 1
	if nm -u "$build/smm/cpu/x86/smm_invocation_loader_composition_gate.o" | \
		grep -q smm_invocation_platform_loader_instance_take; then
		printf '%s\n' 'SMM composition gate retained provider dependency' >&2
		exit 1
	fi
done

printf '%s\n' \
	'.text' \
	'.globl smm_invocation_platform_loader_instance_take' \
	'.type smm_invocation_platform_loader_instance_take, @function' \
	'smm_invocation_platform_loader_instance_take:' \
	'  movl $1, %eax' \
	'  ret' > "$temporary/test-provider.S"
${CC:-cc} -m32 -c "$temporary/test-provider.S" \
	-o "$temporary/test-provider.o"
composition_symbol=smm_invocation_platform_loader_instance_take
for name in GLK ADL Q35; do
	composition="$temporary/$name/ramstage/cpu/x86/smm_invocation_loader_composition.o"
	test "$(nm -u "$composition" | awk -v symbol="$composition_symbol" \
		'$2 == symbol { count++ } END { print count + 0 }')" -eq 1
	ld -m elf_i386 -r "$composition" "$temporary/test-provider.o" \
		-o "$temporary/$name-provider-linked.o"
	test "$(nm -u "$temporary/$name-provider-linked.o" | awk \
		-v symbol="$composition_symbol" \
		'$2 == symbol { count++ } END { print count + 0 }')" -eq 0
	test "$(nm --defined-only "$temporary/$name-provider-linked.o" | awk \
		-v symbol="$composition_symbol" \
		'$3 == symbol { count++ } END { print count + 0 }')" -eq 1
done

for name in GLK ADL Q35; do
	if grep -q '^CONFIG_STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER=y$' \
		"$temporary/$name/full.config"; then
		printf 'MTL provider entered generic profile %s\n' "$name" >&2
		exit 1
	fi
done
grep -q '^CONFIG_STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER=y$' \
	"$temporary/MTL/full.config"
mtl_authority="$temporary/MTL/ramstage/mainboard/starlabs/starbook/variants/mtl/loader_instance_authority.o"
scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/MTL" \
	DOTCONFIG="$temporary/MTL/full.config" KBUILD_KCONFIG="$profile_kconfig" \
	-B "$mtl_authority" >/dev/null
test "$(nm --defined-only "$mtl_authority" | awk \
	'$3 == "smm_invocation_platform_loader_instance_take" { count++ } END { print count + 0 }')" -eq 1
mtl_composition="$temporary/MTL/ramstage/cpu/x86/smm_invocation_loader_composition.o"
test "$(nm -u "$mtl_composition" | awk -v symbol="$composition_symbol" \
	'$2 == symbol { count++ } END { print count + 0 }')" -eq 1
ld -m elf_i386 -r "$mtl_composition" "$mtl_authority" \
	-o "$temporary/mtl-provider-linked.o"
test "$(nm -u "$temporary/mtl-provider-linked.o" | awk \
	-v symbol="$composition_symbol" \
	'$2 == symbol { count++ } END { print count + 0 }')" -eq 0
if ld -m elf_i386 -r "$mtl_composition" "$mtl_authority" \
	"$temporary/test-provider.o" -o "$temporary/duplicate-provider.o" \
	>"$temporary/duplicate-provider.log" 2>&1; then
	printf '%s\n' 'real composition linked duplicate strong providers' >&2
	exit 1
fi
grep -q "multiple definition.*$composition_symbol" \
	"$temporary/duplicate-provider.log"

printf '%s\n' 'SMM invocation loader composition profiles: PASS'
