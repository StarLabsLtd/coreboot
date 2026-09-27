#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=bf50e21e59a5113f59ccba6b6ad02db8beb869ac

if [ ! -f "$root/3rdparty/vboot/firmware/include/vb2_sha.h" ]; then
	printf '%s\n' 'required vboot submodule is not initialized' >&2
	exit 1
fi

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make "$@"
)

profile_kconfig="$temporary/Kconfig"
fsp_headers="$temporary/fsp-headers"
mkdir -p "$fsp_headers"
fsp_headers_rel=$(realpath --relative-to="$root" "$fsp_headers")
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_SMM_APMC_REGISTRY_PROFILE
	bool "test-only SMM APMC registry profile"
	default n
	select SMM_APMC_COMMAND_REGISTRY
EOF

configure()
{
	name=$1
	defconfig=$2
	enabled=$3
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG="configs/config.$defconfig" defconfig >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	if grep -q '^CONFIG_FSP_HEADER_PATH=' "$config"; then
		"$root/util/scripts/config" --file "$config" --set-str \
			FSP_HEADER_PATH "$fsp_headers_rel"
	fi
	if [ "$enabled" = y ]; then
		"$root/util/scripts/config" --file "$config" \
			-e TEST_SMM_APMC_REGISTRY_PROFILE
	fi
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		olddefconfig >/dev/null
	if grep -q '^CONFIG_FSP_HEADER_PATH=' "$config"; then
		grep -Fq "CONFIG_FSP_HEADER_PATH=\"$fsp_headers_rel\"" "$config"
	fi
}

configure OFF starlabs_lite_glk n
if grep -q '^CONFIG_SMM_APMC_COMMAND_REGISTRY=' \
	"$temporary/OFF/full.config"; then
	printf '%s\n' 'SMM APMC registry enabled by default' >&2
	exit 1
fi
off_log="$temporary/config-off.log"
scratch_make -C "$root" UPDATED_SUBMODULES=1 V=1 obj="$temporary/OFF" \
	DOTCONFIG="$temporary/OFF/full.config" KBUILD_KCONFIG="$profile_kconfig" \
	-B "$temporary/OFF/smm/cpu/x86/smm/smm_module_handler.o" \
	"$temporary/OFF/smmstub/cpu/x86/smm/smm_stub.o" >"$off_log" 2>&1

current_handler="$temporary/OFF/smm/cpu/x86/smm/smm_module_handler.o"
handler_source=src/cpu/x86/smm/smm_module_handler.c
handler_command=$(grep "smm_module_handler.o.*$handler_source" "$off_log" | tail -1)
test -n "$handler_command"
base_handler_source="$temporary/smm_module_handler.base.c"
base_handler_object="$temporary/smm_module_handler.base.o"
git -C "$root" show "$base:$handler_source" > "$base_handler_source"
base_handler_command=$(printf '%s\n' "$handler_command" | sed \
	"s|-o $current_handler|-o $base_handler_object|; \
	 s|$handler_source[[:space:]]*$|$base_handler_source|")
(cd "$root" && eval "$base_handler_command")
objcopy --strip-debug "$current_handler" "$temporary/handler.current.o"
objcopy --strip-debug "$base_handler_object" "$temporary/handler.base.o"
cmp "$temporary/handler.current.o" "$temporary/handler.base.o"

for profile in \
	GLK:starlabs_lite_glk \
	ADL:starlabs_lite_adl \
	MTL:starlabs_starbook_mtl \
	Q35:emulation_qemu_x86_q35_smm_tseg; do
	name=${profile%%:*}
	defconfig=${profile#*:}
	configure "$name" "$defconfig" y
	build="$temporary/$name"
	config="$build/full.config"
	grep -q '^CONFIG_SMM_APMC_COMMAND_REGISTRY=y$' "$config"
	grep -q '^CONFIG_SMM_APMC_COMPOSITION_ATTESTED=y$' "$config"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -B \
		"$build/smm/cpu/x86/smm_command.o" \
		"$build/smm/cpu/x86/smm/smm_module_handler.o" \
		"$build/smmstub/cpu/x86/smm/smm_stub.o" >/dev/null
	object="$build/smm/cpu/x86/smm_command.o"
	for symbol in smm_apmc_command_select smm_apmc_command_finish; do
		test "$(nm --defined-only "$object" | awk -v symbol="$symbol" \
			'$3 == symbol { count++ } END { print count + 0 }')" -eq 1
	done
	if nm -u "$object" | grep -q '__atomic_'; then
		printf '%s gained a libatomic dependency\n' "$name" >&2
		exit 1
	fi
	# MTL's complete SMM link needs board-external FSP headers. Its affected
	# objects are built above; Q35 supplies the clean-tree complete link.
	if [ "$name" = Q35 ]; then
		scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
			DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
			-B "$build/smm/smm.elf" >/dev/null
		test -s "$build/smm/smm.elf"
		if nm -u "$build/smm/smm.elf" | grep -q '__atomic_'; then
			printf '%s SMM stage gained a libatomic dependency\n' \
				"$name" >&2
			exit 1
		fi
	fi
done

for source in \
	src/cpu/x86/smm/smm_module_handler.c \
	src/cpu/x86/smm/smm_stub.S; do
	git -C "$root" diff --quiet "$base" -- "$source"
done

printf '%s\n' 'SMM APMC command registry profiles: PASS'
