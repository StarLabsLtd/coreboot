#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=dc5ad02fe29a4db86b7a88c81ae705a218cd07ef

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make "$@"
)

profile_kconfig="$temporary/Kconfig.geometry-profile"
cp "$root/src/Kconfig" "$profile_kconfig"
printf '%s\n' \
	'' \
	'config SMM_SAVE_STATE_GEOMETRY_PROFILE_TEST' \
	'  def_bool y' \
	'  select SMM_INVOCATION_EVIDENCE' \
	'  select SMM_INVOCATION_ENTRY_PLATFORM' \
	'  select SMM_INVOCATION_FAIL_STOP_PLATFORM' \
	'  select SMM_INVOCATION_ENTRY' \
	'  select SMM_INVOCATION_INTEL_ADAPTER' >> "$profile_kconfig"

stm_kconfig="$temporary/Kconfig.geometry-stm-profile"
cp "$profile_kconfig" "$stm_kconfig"
printf '%s\n' \
	'' \
	'config SMM_SAVE_STATE_GEOMETRY_STM_PROFILE_TEST' \
	'  def_bool y' \
	'  select ENABLE_VMX' \
	'  select STM' >> "$stm_kconfig"

configure()
{
	name=$1
	defconfig=$2
	kconfig=$3
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" defconfig KBUILD_KCONFIG="$kconfig" \
		KBUILD_DEFCONFIG="configs/config.$defconfig" >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$kconfig" olddefconfig >/dev/null
}

build_objects()
{
	name=$1
	kconfig=$2
	build="$temporary/$name"
	config="$build/full.config"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$kconfig" -j4 \
		"$build/smm/cpu/x86/smm/save_state_geometry.o" \
		"$build/smm/cpu/x86/smm/smm_module_handler.o" \
		"$build/smm/soc/intel/common/block/smm/invocation_adapter.o" >/dev/null
	test -s "$build/smm/cpu/x86/smm/save_state_geometry.o"
	test -s "$build/smm/cpu/x86/smm/smm_module_handler.o"
	test -s "$build/smm/soc/intel/common/block/smm/invocation_adapter.o"
}

configure mtl_off starlabs_starbook_mtl "$root/src/Kconfig"
! grep -q '^CONFIG_STM=y$' "$temporary/mtl_off/full.config"
	! grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER=y$' \
	"$temporary/mtl_off/full.config"
! grep -q '^CONFIG_SMM_SAVE_STATE_GEOMETRY=y$' \
	"$temporary/mtl_off/full.config"
scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/mtl_off" \
	DOTCONFIG="$temporary/mtl_off/full.config" V=1 -B -j4 \
	"$temporary/mtl_off/smm/smm" \
	>"$temporary/mtl_off/build.log" 2>&1
test ! -e "$temporary/mtl_off/smm/cpu/x86/smm/save_state_geometry.o"
test ! -e "$temporary/mtl_off/smm/soc/intel/common/block/smm/invocation_adapter.o"

handler="$temporary/mtl_off/smm/cpu/x86/smm/smm_module_handler.o"
handler_command=$(grep 'smm_module_handler\.o.*smm_module_handler\.c' \
	"$temporary/mtl_off/build.log" | tail -1)
test -n "$handler_command"
base_source="$temporary/smm_module_handler.base.c"
base_object="$temporary/smm_module_handler.base.o"
git -C "$root" show "$base:src/cpu/x86/smm/smm_module_handler.c" > \
	"$base_source"
base_command=$(printf '%s\n' "$handler_command" | sed \
	"s|-o $handler|-o $base_object|; s|src/cpu/x86/smm/smm_module_handler.c[[:space:]]*$|$base_source|")
(cd "$root" && eval "$base_command")
objcopy --strip-debug "$handler" "$temporary/handler.current.o"
objcopy --strip-debug "$base_object" "$temporary/handler.base.o"
cmp "$temporary/handler.current.o" "$temporary/handler.base.o"

cp "$temporary/mtl_off/smm/smm" "$temporary/smm.current"
cp "$base_object" "$handler"
rm -f "$temporary/mtl_off/smm/smm.a" \
	"$temporary/mtl_off/smm/smm.a.tmp" "$temporary/mtl_off/smm/smm.o" \
	"$temporary/mtl_off/smm/smm.elf" "$temporary/mtl_off/smm/smm.elf.rmod" \
	"$temporary/mtl_off/smm/smm"
scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/mtl_off" \
	DOTCONFIG="$temporary/mtl_off/full.config" -j4 \
	"$temporary/mtl_off/smm/smm" >/dev/null
cmp "$temporary/smm.current" "$temporary/mtl_off/smm/smm"

configure mtl_on starlabs_starbook_mtl "$profile_kconfig"
! grep -q '^CONFIG_STM=y$' "$temporary/mtl_on/full.config"
grep -q '^CONFIG_SMM_SAVE_STATE_GEOMETRY=y$' \
	"$temporary/mtl_on/full.config"
build_objects mtl_on "$profile_kconfig"

configure mtl_stm starlabs_starbook_mtl "$stm_kconfig"
grep -q '^CONFIG_STM=y$' "$temporary/mtl_stm/full.config"
grep -q '^CONFIG_SMM_SAVE_STATE_GEOMETRY=y$' \
	"$temporary/mtl_stm/full.config"
build_objects mtl_stm "$stm_kconfig"

printf 'SMM save-state geometry production profiles passed\n'
