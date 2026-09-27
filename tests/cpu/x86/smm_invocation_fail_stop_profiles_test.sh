#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=3d6407d3cb6a430e6326a1bfb70ee525d3dec39d

scratch_make()
(
	# This script is also a recipe of the top-level coreboot Makefile. Do not
	# let that outer recursion level or jobserver alter these isolated builds.
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES
	exec make "$@"
)

configure()
{
	name=$1
	defconfig=$2
	selector=$3
	build="$temporary/$name"
	config="$build/full.config"
	kconfig="$build/Kconfig"
	mkdir -p "$build"
	cp "$root/src/Kconfig" "$kconfig"
	if [ -n "$selector" ]; then
		printf '%s\n' '' "config ${name}_PROFILE" '  def_bool y' \
			"  select $selector" >> "$kconfig"
	fi
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		KBUILD_KCONFIG="$kconfig" KBUILD_DEFCONFIG="$defconfig" \
		defconfig >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		KBUILD_KCONFIG="$kconfig" olddefconfig >/dev/null
}

configure Q35_OFF configs/config.emulation_qemu_x86_q35_smm_tseg ''
configure MTL_OFF configs/config.starlabs_starbook_mtl ''
for config in "$temporary/Q35_OFF/full.config" "$temporary/MTL_OFF/full.config"; do
	if grep -Eq '^CONFIG_(SMM_INVOCATION_FAIL_STOP_PLATFORM|Q35_SMM_INVOCATION_FAIL_STOP_TEST|STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP)=y$' \
		"$config"; then
		printf '%s\n' 'fail-stop provider unexpectedly enabled by default' >&2
		exit 1
	fi
done

# A representative existing SMM object remains byte-identical with both
# providers disabled.
off_build="$temporary/Q35_OFF"
off_config="$off_build/full.config"
off_log="$temporary/config-off-build.log"
scratch_make -C "$root" UPDATED_SUBMODULES=1 V=1 obj="$off_build" \
	DOTCONFIG="$off_config" KBUILD_KCONFIG="$off_build/Kconfig" -B \
	"$off_build/smm/cpu/x86/smm/smm_module_handler.o" >"$off_log" 2>&1
command=$(grep 'smm_module_handler\.o.*smm_module_handler\.c' "$off_log" | tail -1)
test -n "$command"
base_source="$temporary/smm_module_handler.base.c"
git -C "$root" show "$base:src/cpu/x86/smm/smm_module_handler.c" > "$base_source"
base_object="$temporary/smm_module_handler.base.o"
current_object="$off_build/smm/cpu/x86/smm/smm_module_handler.o"
base_command=$(printf '%s\n' "$command" | sed \
	"s|-o $current_object|-o $base_object|; s|src/cpu/x86/smm/smm_module_handler.c[[:space:]]*$|$base_source|")
(cd "$root" && eval "$base_command")
objcopy --strip-debug "$current_object" "$temporary/current.stripped.o"
objcopy --strip-debug "$base_object" "$temporary/base.stripped.o"
cmp "$temporary/current.stripped.o" "$temporary/base.stripped.o"

configure Q35_ON configs/config.emulation_qemu_x86_q35_smm_tseg \
	Q35_SMM_INVOCATION_FAIL_STOP_TEST
configure MTL_ON configs/config.starlabs_starbook_mtl \
	STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP

for name in Q35_ON MTL_ON; do
	config="$temporary/$name/full.config"
	grep -q '^CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM=y$' "$config"
	grep -q '^#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1$' \
		"$temporary/$name/config.h"
done
grep -q '^CONFIG_Q35_SMM_INVOCATION_FAIL_STOP_TEST=y$' \
	"$temporary/Q35_ON/full.config"
if grep -q '^CONFIG_STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP=y$' \
	"$temporary/Q35_ON/full.config"; then
	printf '%s\n' 'MTL provider entered the Q35 profile' >&2
	exit 1
fi
grep -q '^CONFIG_STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP=y$' \
	"$temporary/MTL_ON/full.config"
if grep -q '^CONFIG_Q35_SMM_INVOCATION_FAIL_STOP_TEST=y$' \
	"$temporary/MTL_ON/full.config"; then
	printf '%s\n' 'Q35 provider entered the MTL profile' >&2
	exit 1
fi

scratch_make -C "$root" UPDATED_SUBMODULES=1 -j4 obj="$temporary/Q35_ON" \
	DOTCONFIG="$temporary/Q35_ON/full.config" \
	KBUILD_KCONFIG="$temporary/Q35_ON/Kconfig" \
	"$temporary/Q35_ON/smm/mainboard/emulation/qemu-q35/smm_invocation_fail_stop.o" \
	>/dev/null
scratch_make -C "$root" UPDATED_SUBMODULES=1 -j4 obj="$temporary/MTL_ON" \
	DOTCONFIG="$temporary/MTL_ON/full.config" \
	KBUILD_KCONFIG="$temporary/MTL_ON/Kconfig" \
	"$temporary/MTL_ON/smm/mainboard/starlabs/starbook/variants/mtl/smm_invocation_fail_stop.o" \
	>/dev/null
for object in \
	"$temporary/Q35_ON/smm/mainboard/emulation/qemu-q35/smm_invocation_fail_stop.o" \
	"$temporary/MTL_ON/smm/mainboard/starlabs/starbook/variants/mtl/smm_invocation_fail_stop.o"; do
	[ "$(nm --defined-only "$object" | \
		awk '$3 == "smm_invocation_platform_fail_stop" { count++ } END { print count + 0 }')" \
		-eq 1 ]
done

printf '%s\n' 'SMM invocation fail-stop provider profiles: PASS'
