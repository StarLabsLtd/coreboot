#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/fsp"

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES
	exec "${MAKE:-make}" VBOOT_SOURCE="$root/3rdparty/vboot" "$@"
)

test -f "$root/3rdparty/vboot/firmware/2lib/include/2api.h" || {
	echo 'vboot submodule is required for presence arm profiles' >&2
	exit 1
}

profile_kconfig="$temporary/Kconfig"
head -n 3 "$root/src/Kconfig" > "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES
	bool "test-only presence arm prerequisites"
	default n
	select BOOTMEM_ALIGNED_RESERVATIONS
	select BOOTMEM_ALIGNED_RESERVATION_RECEIPT
	select SMM_INVOCATION_FAIL_STOP_PLATFORM
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select SMM_INVOCATION_LOADER_INSTANCE_PLATFORM
	select SMM_INVOCATION_LOADER_COMPOSITION

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile_kconfig"

lib_kconfig="$temporary/lib.Kconfig"
sed -e 's/depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER$/'\
'depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER || '\
'TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PLATFORM$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ARM$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" \
	"$profile_kconfig"

for profile in emulation_qemu_x86_q35_smm_tseg starlabs_starbook_mtl; do
	case "$profile" in
		emulation*) name=q35 ;;
		*) name=mtl ;;
	esac
	config="$temporary/$name.config"
	build="$temporary/$name"
	cp "$root/configs/config.$profile" "$config"
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
		-e TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES
	if grep -q '^CONFIG_FSP_HEADER_PATH=' "$config"; then
		"$root/util/scripts/config" --file "$config" \
			--set-str FSP_HEADER_PATH "$temporary/fsp"
	fi
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" \
		obj="$build" olddefconfig
	grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ARM=y$' "$config"
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" obj="$build" -B \
		"$build/smm/lib/payload_mm_authvar_presence_arm.o"
	file "$build/smm/lib/payload_mm_authvar_presence_arm.o" | grep -q 'ELF 32-bit'
	! nm -u "$build/smm/lib/payload_mm_authvar_presence_arm.o" | \
		grep -Eq '__atomic|libatomic'
done

printf '%s\n' 'Payload-MM presence arm Q35/MTL profiles: PASS'
