#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=7df9b7efb7859069f5555780520d04600e7a4542

off_build="$temporary/config-off"
off_config="$off_build/full.config"
mkdir -p "$off_build"
make -C "$root" obj="$off_build" DOTCONFIG="$off_config" defconfig \
	KBUILD_DEFCONFIG=configs/config.starlabs_lite_glk >/dev/null
"$root/util/scripts/config" --file "$off_config" -e ANY_TOOLCHAIN
make -C "$root" obj="$off_build" DOTCONFIG="$off_config" olddefconfig >/dev/null
if grep -q '^CONFIG_SMM_INVOCATION_TOPOLOGY=y$' "$off_config"; then
	printf '%s\n' 'topology unexpectedly enabled in config-off profile' >&2
	exit 1
fi
off_log="$temporary/config-off-build.log"
make -C "$root" V=1 obj="$off_build" DOTCONFIG="$off_config" -B \
	"$off_build/ramstage/cpu/x86/smm/smm_module_loader.o" \
	"$off_build/smm/cpu/x86/smm/smm_module_handler.o" \
	>"$off_log" 2>&1
compile_command=$(grep 'smm_module_loader\.o.*smm_module_loader\.c' "$off_log" | tail -1)
test -n "$compile_command"
base_source="$temporary/smm_module_loader.base.c"
git -C "$root" show "$base:src/cpu/x86/smm/smm_module_loader.c" > "$base_source"
base_object="$temporary/smm_module_loader.base.o"
current_object="$off_build/ramstage/cpu/x86/smm/smm_module_loader.o"
base_command=$(printf '%s\n' "$compile_command" | \
	sed "s|-o $current_object|-o $base_object|; s|src/cpu/x86/smm/smm_module_loader.c$|$base_source|")
(cd "$root" && eval "$base_command")
objcopy --strip-debug "$current_object" "$temporary/current.stripped.o"
objcopy --strip-debug "$base_object" "$temporary/base.stripped.o"
cmp "$temporary/current.stripped.o" "$temporary/base.stripped.o"

handler_command=$(grep 'smm_module_handler\.o.*smm_module_handler\.c' \
	"$off_log" | tail -1)
test -n "$handler_command"
handler_base_source="$temporary/smm_module_handler.base.c"
git -C "$root" show "$base:src/cpu/x86/smm/smm_module_handler.c" > \
	"$handler_base_source"
handler_base_object="$temporary/smm_module_handler.base.o"
handler_current_object="$off_build/smm/cpu/x86/smm/smm_module_handler.o"
handler_base_command=$(printf '%s\n' "$handler_command" | \
	sed "s|-o $handler_current_object|-o $handler_base_object|; s|src/cpu/x86/smm/smm_module_handler.c$|$handler_base_source|")
(cd "$root" && eval "$handler_base_command")
objcopy --strip-debug "$handler_current_object" "$temporary/handler-current.stripped.o"
objcopy --strip-debug "$handler_base_object" "$temporary/handler-base.stripped.o"
cmp "$temporary/handler-current.stripped.o" "$temporary/handler-base.stripped.o"

profile_kconfig="$temporary/Kconfig.topology-profile"
cp "$root/src/Kconfig" "$profile_kconfig"
printf '%s\n' \
	'' \
	'config SMM_INVOCATION_TOPOLOGY_PROFILE_TEST' \
	'  def_bool y' \
	'  select SMM_INVOCATION_EVIDENCE' \
	'  select SMM_INVOCATION_ENTRY_PLATFORM' \
	'  select SMM_INVOCATION_ENTRY' \
	'  select SMM_INVOCATION_INTEL_ADAPTER' \
	'  select SMM_INVOCATION_TOPOLOGY' >> "$profile_kconfig"

for profile in starlabs_lite_glk starlabs_lite_adl starlabs_starbook_mtl; do
	build="$temporary/$profile"
	config="$build/full.config"
	mkdir -p "$build"
	make -C "$root" obj="$build" DOTCONFIG="$config" defconfig \
		KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG="configs/config.$profile" >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	make -C "$root" obj="$build" DOTCONFIG="$config" \
		KBUILD_KCONFIG="$profile_kconfig" olddefconfig >/dev/null
	for symbol in SMM_INVOCATION_EVIDENCE SMM_INVOCATION_ENTRY_PLATFORM \
		SMM_INVOCATION_ENTRY SMM_INVOCATION_INTEL_ADAPTER \
		SMM_INVOCATION_TOPOLOGY; do
		grep -q "^CONFIG_${symbol}=y$" "$config"
		grep -q "^#define CONFIG_${symbol} 1$" "$build/config.h"
	done
	make -C "$root" obj="$build" DOTCONFIG="$config" \
		KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$build/ramstage/cpu/x86/smm/smm_module_loader.o" \
		"$build/ramstage/cpu/x86/smm_invocation_topology.o" \
		"$build/smm/cpu/x86/smm/smm_module_handler.o" \
		"$build/smmstub/cpu/x86/smm/smm_stub.o" >/dev/null
done

printf '%s\n' 'SMM invocation topology profile compiles: PASS'
