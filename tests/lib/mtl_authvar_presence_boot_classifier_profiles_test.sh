#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "$root/../.mtl-presence-boot-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=aa179eedb561cd5515c5904cc0fc240799de2231
baseline="$temporary/base"

test -f "$root/3rdparty/vboot/firmware/include/vb2_sha.h"
test -f "$root/3rdparty/stm/Readme.STMPE"

scratch_make()
(
	tree=$1
	shift
	vboot_source="$root/3rdparty/vboot"
	if [ "$tree" = "$baseline" ]; then
		vboot_source=3rdparty/vboot
	fi
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make BUILD_TIMELESS=1 KERNELVERSION=coreboot-mtl-presence-boot-test \
		VBOOT_SOURCE="$vboot_source" -C "$tree" "$@"
)

profile_kconfig="$temporary/Kconfig.mtl-presence-boot-profile"
head -n 3 "$root/src/Kconfig" > "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_MTL_AUTHVAR_PRESENCE_BOOT_CLASSIFIER_PROFILE
	bool "test-only MTL presence boot classifier"
	default n
	select SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile_kconfig"

on_build="$temporary/MTL_ON"
on_config="$on_build/full.config"
mkdir -p "$on_build"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$on_build" \
	DOTCONFIG="$on_config" KBUILD_KCONFIG="$profile_kconfig" \
	KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
"$root/util/scripts/config" --file "$on_config" -e ANY_TOOLCHAIN \
	-e TEST_MTL_AUTHVAR_PRESENCE_BOOT_CLASSIFIER_PROFILE -e PAYLOAD_NONE -d LTO
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$on_build" \
	DOTCONFIG="$on_config" KBUILD_KCONFIG="$profile_kconfig" \
	olddefconfig >/dev/null
grep -q '^CONFIG_SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER=y$' \
	"$on_config"
classifier="$on_build/romstage/soc/intel/meteorlake/authvar_presence_boot_classifier.o"
scratch_make "$root" UPDATED_SUBMODULES=1 obj="$on_build" \
	DOTCONFIG="$on_config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
	"$on_build/cbfs/fallback/romstage.debug" >/dev/null
test -s "$classifier"
file "$classifier" | grep -q 'ELF 32-bit'
test "$(find "$on_build" -name authvar_presence_boot_classifier.o | wc -l)" -eq 1
# No live caller exists in this slice, so archive extraction must not retain it.
! nm "$on_build/cbfs/fallback/romstage.debug" | \
	grep -q ' [Tt] mtl_authvar_presence_boot_classify$'
! nm -u "$classifier" | grep -Eq '__atomic|libatomic'
if objdump -d "$classifier" | grep -Eq \
	'(^|[[:space:]])(in|ins|out|outs)[bwl]?[[:space:]]'; then
	printf '%s\n' 'MTL boot classifier gained an I/O instruction' >&2
	exit 1
fi

cat > "$temporary/consumer.c" <<'EOF'
#include <soc/authvar_presence_boot_classifier.h>

enum cb_err consume(const struct chipset_power_state *power_state,
	struct mtl_authvar_presence_boot_evidence *evidence)
{
	return mtl_authvar_presence_boot_classify(power_state, evidence);
}
EOF
${CC:-cc} -m32 -march=i686 -std=gnu11 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -D__COREBOOT__ \
	-I"$root/src/include" -I"$root/src/commonlib/bsd/include" \
	-I"$root/src/soc/intel/meteorlake/include" \
	-c "$temporary/consumer.c" -o "$temporary/consumer.o"
ld -m elf_i386 -r "$temporary/consumer.o" "$classifier" \
	-o "$temporary/classifier-consumer.o"
! nm -u "$temporary/classifier-consumer.o" | \
	grep -q mtl_authvar_presence_boot_classify
test "$(nm "$temporary/classifier-consumer.o" | \
	grep -c ' [Tt] mtl_authvar_presence_boot_classify$')" -eq 1

mkdir -p "$baseline"
git -C "$root" archive "$base" | tar -x -C "$baseline"
rmdir "$baseline/3rdparty/vboot" "$baseline/3rdparty/stm"
ln -s "$root/3rdparty/vboot" "$baseline/3rdparty/vboot"
ln -s "$root/3rdparty/stm" "$baseline/3rdparty/stm"
ln -s "$root/../../intel_fsp" "$temporary/intel_fsp"

build_off()
{
	tree=$1
	build_name=$2
	profile=$3
	build="$temporary/$build_name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		KBUILD_DEFCONFIG="configs/config.$profile" defconfig >/dev/null
	"$tree/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO \
		-e PAYLOAD_NONE
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		olddefconfig >/dev/null
	! grep -q '^CONFIG_SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER=y$' \
		"$config"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" DOTCONFIG="$config" \
		-j4 "$build/cbfs/fallback/romstage.debug" >/dev/null
}

for entry in \
	emulation_qemu_x86_q35_authvar_media:Q35 \
	starlabs_lite_glk:GLK \
	starlabs_lite_adl:ADL \
	starlabs_starbook_mtl:MTL; do
	profile=${entry%%:*}
	label=${entry##*:}
	build_off "$root" "$label-current" "$profile"
	build_off "$baseline" "$label-base" "$profile"
	sed '/CONFIG_SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER/d' \
		"$temporary/$label-current/full.config" > "$temporary/$label-current.config"
	sed '/CONFIG_SOC_INTEL_METEORLAKE_AUTHVAR_PRESENCE_BOOT_CLASSIFIER/d' \
		"$temporary/$label-base/full.config" > "$temporary/$label-base.config"
	cmp "$temporary/$label-current.config" "$temporary/$label-base.config"
	objcopy -O binary \
		"$temporary/$label-current/cbfs/fallback/romstage.debug" \
		"$temporary/$label-current.romstage"
	objcopy -O binary \
		"$temporary/$label-base/cbfs/fallback/romstage.debug" \
		"$temporary/$label-base.romstage"
	cmp "$temporary/$label-current.romstage" "$temporary/$label-base.romstage"
done

printf '%s\n' 'MTL presence boot classifier profiles: PASS'
