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
	echo 'vboot submodule is required for transaction profiles' >&2
	exit 1
}

profile_kconfig="$temporary/Kconfig"
head -n 3 "$root/src/Kconfig" > "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_AUTHVAR_PRESENCE_TRANSACTION_PREREQUISITES
	bool "test-only transaction prerequisites"
	default n
	select BOOTMEM_ALIGNED_RESERVATIONS
	select BOOTMEM_ALIGNED_RESERVATION_RECEIPT

config TEST_AUTHVAR_PRESENCE_TRANSACTION_STACK
	bool "test-only transaction stack profile"
	default n

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_AUTHVAR_PRESENCE_TRANSACTION_STACK
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile_kconfig"
lib_kconfig="$temporary/lib.Kconfig"
sed -e 's/depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER$/'\
'depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER || '\
'TEST_AUTHVAR_PRESENCE_TRANSACTION_PREREQUISITES/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 '\
's/^\tdefault n$/\tdefault y if '\
'TEST_AUTHVAR_PRESENCE_TRANSACTION_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF$/,+2 '\
's/^\tdefault n$/\tdefault y if '\
'TEST_AUTHVAR_PRESENCE_TRANSACTION_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 '\
's/^\tdefault n$/\tdefault y if '\
'TEST_AUTHVAR_PRESENCE_TRANSACTION_PREREQUISITES\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" \
	"$profile_kconfig"

configure()
{
	profile=$1
	name=$2
	enabled=$3
	config="$temporary/$name.config"
	build="$temporary/$name"

	cp "$root/configs/config.$profile" "$config"
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
		-e TEST_AUTHVAR_PRESENCE_TRANSACTION_PREREQUISITES
	if [ "$enabled" = y ]; then
		"$root/util/scripts/config" --file "$config" \
			-e TEST_AUTHVAR_PRESENCE_TRANSACTION_STACK
	fi
	if grep -q '^CONFIG_FSP_HEADER_PATH=' "$config"; then
		"$root/util/scripts/config" --file "$config" \
			--set-str FSP_HEADER_PATH "$temporary/fsp"
	fi
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" \
		obj="$build" olddefconfig
	stack_value=$(sed -n 's/^CONFIG_SMM_MODULE_STACK_SIZE=//p' "$config")
	if [ "$enabled" = n ]; then
		test "$((stack_value))" -lt 16384
		! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION=y$' \
			"$config"
	else
		test "$((stack_value))" -ge 16384
		grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION=y$' \
			"$config"
	fi
}

configure emulation_qemu_x86_q35_smm_tseg q35-low n
configure emulation_qemu_x86_q35_smm_tseg q35 y
configure starlabs_starbook_mtl mtl y

for name in q35 mtl; do
	config="$temporary/$name.config"
	build="$temporary/$name"
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" obj="$build" -B \
		"$build/smm/lib/payload_mm_authvar_presence_transaction.o" \
		"$build/smm/lib/payload_mm_authvar_presence_transaction_receiver.o" \
		"$build/smm/lib/bootmem_reservation_receipt.o"
	for object in payload_mm_authvar_presence_transaction \
		payload_mm_authvar_presence_transaction_receiver \
		bootmem_reservation_receipt; do
		file "$build/smm/lib/$object.o" | grep -q 'ELF 32-bit'
		! nm -u "$build/smm/lib/$object.o" | grep -Eq '__atomic|libatomic'
	done
done

# Q35 is self-contained and provides the clean full-stage link proof. The MTL
# full link needs StarLabs' external FSP headers, so its real SMM objects are
# the reproducible platform proof in this slice.
scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
	KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$temporary/q35.config" \
	obj="$temporary/q35" \
	"$temporary/q35/smm/smm.elf"
! nm -u "$temporary/q35/smm/smm.elf" | grep -Eq '__atomic|libatomic'

# Default-off and explicitly disabled Q35 SMM images must be byte-identical.
for name in off-default off-explicit; do
	config="$temporary/$name.config"
	build="$temporary/$name"
	cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" "$config"
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	if [ "$name" = off-explicit ]; then
		"$root/util/scripts/config" --file "$config" \
			-d PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION
	fi
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 DOTCONFIG="$config" \
		obj="$build" olddefconfig
	! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION=y$' "$config"
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 DOTCONFIG="$config" \
		obj="$build" "$build/smm/smm.elf"
	objcopy --strip-all "$build/smm/smm.elf" "$temporary/$name.stripped"
done
cmp "$temporary/off-default.stripped" "$temporary/off-explicit.stripped"

printf '%s\n' 'Payload-MM presence transaction profiles: PASS'
