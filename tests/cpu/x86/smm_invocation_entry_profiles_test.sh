#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
profile_kconfig="$temporary/Kconfig.entry-profile"
cp "$root/src/Kconfig" "$profile_kconfig"
printf '%s\n' \
	'' \
	'config SMM_INVOCATION_ENTRY_PROFILE_TEST' \
	'  def_bool y' \
	'  select SMM_INVOCATION_EVIDENCE' \
	'  select SMM_INVOCATION_ENTRY_PLATFORM' \
	'  select SMM_INVOCATION_FAIL_STOP_PLATFORM' \
	'  select SMM_INVOCATION_ENTRY' \
	'  select SMM_INVOCATION_INTEL_ADAPTER' >> "$profile_kconfig"

for profile in starlabs_lite_glk starlabs_lite_adl starlabs_starbook_mtl; do
	build="$temporary/$profile"
	config="$build/full.config"
	mkdir -p "$build"
	make -C "$root" obj="$build" DOTCONFIG="$config" defconfig \
		KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG="configs/config.$profile" >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	make -C "$root" obj="$build" DOTCONFIG="$config" \
		KBUILD_KCONFIG="$profile_kconfig" olddefconfig \
		>/dev/null
	for symbol in SMM_INVOCATION_EVIDENCE SMM_INVOCATION_FAIL_STOP_PLATFORM \
		SMM_INVOCATION_ENTRY_PLATFORM \
		SMM_INVOCATION_ENTRY SMM_INVOCATION_INTEL_ADAPTER; do
		grep -q "^CONFIG_${symbol}=y$" "$config"
		grep -q "^#define CONFIG_${symbol} 1$" "$build/config.h"
	done
	make -C "$root" obj="$build" DOTCONFIG="$config" \
		KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$build/smm/cpu/x86/smm_invocation_entry.o" \
		"$build/smm/cpu/x86/smm_invocation_evidence.o" \
		"$build/smm/soc/intel/common/block/smm/invocation_adapter.o" \
		"$build/smmstub/cpu/x86/smm/smm_stub.o" >/dev/null
done

printf '%s\n' 'SMM invocation entry profile compiles: PASS'
