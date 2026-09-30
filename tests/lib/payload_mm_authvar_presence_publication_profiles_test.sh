#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/fsp"

scratch_make()
{
	env -u MAKELEVEL -u MAKEFLAGS -u MFLAGS -u MAKEOVERRIDES \
		"${MAKE:-make}" "$@"
}

profile_kconfig="$temporary/Kconfig"
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_AUTHVAR_PRESENCE_PUBLICATION_PROFILE
	def_bool y
	select BOOTMEM_ALIGNED_RESERVATIONS
EOF

configure()
{
	name=$1
	config=$2
	build=$3

	cp "$root/configs/config.$name" "$config"
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	if grep -q '^CONFIG_FSP_HEADER_PATH=' "$config"; then
		"$root/util/scripts/config" --file "$config" \
			--set-str FSP_HEADER_PATH "$temporary/fsp"
	fi
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" \
		obj="$build" olddefconfig
	grep -q '^CONFIG_BOOTMEM_ALIGNED_RESERVATIONS=y$' "$config"
}

for profile in emulation_qemu_x86_q35_smm_tseg starlabs_starbook_mtl; do
	config="$temporary/$profile.config"
	build="$temporary/$profile"
	configure "$profile" "$config" "$build"
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" obj="$build" \
		CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION=y \
		CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER=y -B \
		"$build/ramstage/lib/payload_mm_authvar_presence_publication.o" \
		"$build/ramstage/lib/payload_mm_authvar_presence_producer.o"
	for object in payload_mm_authvar_presence_publication \
		payload_mm_authvar_presence_producer; do
		file "$build/ramstage/lib/$object.o" | grep -q 'ELF 32-bit'
		if nm "$build/ramstage/lib/$object.o" | grep -q '__atomic_'; then
			echo "$profile $object gained a libatomic dependency" >&2
			exit 1
		fi
	done
done

# With publication disabled, moving the guarded call must not change the
# stripped table-writer object or its record order.
off_config="$temporary/q35-off.config"
off_build="$temporary/q35-off"
off_log="$temporary/q35-off.log"
cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" "$off_config"
"$root/util/scripts/config" --file "$off_config" -e ANY_TOOLCHAIN
scratch_make -s -C "$root" UPDATED_SUBMODULES=1 DOTCONFIG="$off_config" \
	obj="$off_build" olddefconfig
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION=y$' "$off_config"
scratch_make -C "$root" V=1 UPDATED_SUBMODULES=1 DOTCONFIG="$off_config" \
	obj="$off_build" -B "$off_build/ramstage/lib/coreboot_table.o" \
	>"$off_log" 2>&1

mkdir -p "$temporary/source"
cp "$root/src/lib/coreboot_table.c" "$temporary/source/coreboot_table.c"
test "$(grep -c 'if (CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION) &&' \
	"$temporary/source/coreboot_table.c")" -eq 2
test "$(grep -c 'die("Authenticated-variable presence publication failed' \
	"$temporary/source/coreboot_table.c")" -eq 2
current="$off_build/ramstage/lib/coreboot_table.o"
current_object="$temporary/coreboot_table.current.o"
off_object="$temporary/coreboot_table.off.o"
command=$(grep -- "-c -o $current src/lib/coreboot_table.c" "$off_log" | tail -1)
test -n "$command"
compile=$(printf '%s\n' "$command" | sed \
	"s|src/lib/coreboot_table.c|$temporary/source/coreboot_table.c|")
current_command=$(printf '%s\n' "$compile" | sed \
	"s|-o $current|-o $current_object|")
(cd "$root" && eval "$current_command")
awk '
	/if \(CONFIG\(PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION\) &&/ { skip = 1 }
	skip { print ""; if (/die\("Authenticated-variable presence publication failed/) skip = 0; next }
	{ print }
' "$temporary/source/coreboot_table.c" > "$temporary/source/transformed.c"
! cmp -s "$temporary/source/coreboot_table.c" \
	"$temporary/source/transformed.c"
mv "$temporary/source/transformed.c" "$temporary/source/coreboot_table.c"
off_command=$(printf '%s\n' "$compile" | sed "s|-o $current|-o $off_object|")
(cd "$root" && eval "$off_command")
objcopy --strip-all "$current_object" "$temporary/current.stripped.o"
objcopy --strip-all "$off_object" "$temporary/off.stripped.o"
cmp "$temporary/current.stripped.o" "$temporary/off.stripped.o"

printf '%s\n' 'Payload-MM presence publication profiles: PASS'
