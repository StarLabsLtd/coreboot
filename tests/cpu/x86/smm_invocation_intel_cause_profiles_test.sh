#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.intel-cause-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
base=23e5df6e6f529106c041c8481f3c29790417fcfe

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make "$@"
)

profile_kconfig="$temporary/Kconfig.intel-cause-profile"
cp "$root/src/Kconfig" "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_MTL_INTEL_CAUSE_PROFILE
	bool "test-only MTL Intel cause profile"
	default n
	select ENABLE_EARLY_DMA_PROTECTION
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP
	select SMM_INVOCATION_INTEL_CAUSE
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

configure MTL_OFF starlabs_starbook_mtl ''
configure Q35_OFF emulation_qemu_x86_q35_smm_tseg ''
off_build="$temporary/MTL_OFF"
off_config="$off_build/full.config"
! grep -q '^CONFIG_SMM_INVOCATION_INTEL_CAUSE=y$' "$off_config"
! grep -q '^CONFIG_SMM_INVOCATION_INTEL_CAUSE=y$' \
	"$temporary/Q35_OFF/full.config"
if ! scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$off_build" \
	DOTCONFIG="$off_config" KBUILD_KCONFIG="$profile_kconfig" -B -j4 \
	"$off_build/smm/smm" >"$off_build/smm-build.log" 2>&1; then
	cat "$off_build/smm-build.log" >&2
	exit 1
fi
test ! -e "$off_build/smm/soc/intel/common/block/smm/invocation_cause.o"
scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$temporary/Q35_OFF" \
	DOTCONFIG="$temporary/Q35_OFF/full.config" \
	KBUILD_KCONFIG="$profile_kconfig" -j4 \
	"$temporary/Q35_OFF/smm/smm" >/dev/null
test ! -e \
	"$temporary/Q35_OFF/smm/soc/intel/common/block/smm/invocation_cause.o"

# Build the exact base revision beside the worktree and compare default-off
# linked SMM images, rather than inferring identity from object absence.
baseline="$temporary/base"
mkdir -p "$baseline"
git -C "$root" archive "$base" | tar -x -C "$baseline"
ln -s "$root/../intel_fsp" "$temporary/intel_fsp"
for submodule in vboot stm; do
	rmdir "$baseline/3rdparty/$submodule"
	ln -s "$root/3rdparty/$submodule" \
		"$baseline/3rdparty/$submodule"
done

build_base()
{
	name=$1
	defconfig=$2
	build="$temporary/BASE_$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_DEFCONFIG="configs/config.$defconfig" \
		defconfig >/dev/null
	"$baseline/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" olddefconfig >/dev/null
	scratch_make -C "$baseline" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" -j4 "$build/smm/smm" >/dev/null
	cmp "$temporary/$name/smm/smm" "$build/smm/smm"
}

build_base MTL_OFF starlabs_starbook_mtl
build_base Q35_OFF emulation_qemu_x86_q35_smm_tseg

configure MTL_ON starlabs_starbook_mtl TEST_MTL_INTEL_CAUSE_PROFILE
for name in MTL_ON; do
	build="$temporary/$name"
	config="$build/full.config"
	for symbol in SMM_INVOCATION_LOADER_COMPOSITION \
		SMM_INVOCATION_INTEL_CAUSE; do
		grep -q "^CONFIG_${symbol}=y$" "$config"
	done
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$build/smm/soc/intel/common/block/smm/invocation_cause.o" \
		"$build/bootblock/soc/intel/common/block/pmc/pmclib.o" \
		"$build/romstage/soc/intel/common/block/pmc/pmclib.o" \
		"$build/ramstage/soc/intel/common/block/pmc/pmclib.o" \
		"$build/postcar/soc/intel/common/block/pmc/pmclib.o" \
		"$build/verstage/soc/intel/common/block/pmc/pmclib.o" \
		"$build/smm/smm" >/dev/null
	cause="$build/smm/soc/intel/common/block/smm/invocation_cause.o"
	test "$(nm --defined-only "$cause" | awk \
		'$3 == "intel_smm_invocation_private_cause" { count++ } END { print count + 0 }')" -eq 1
	test "$(find "$build" -name invocation_cause.o -print | wc -l)" -eq 1
	for stage in bootblock romstage ramstage postcar verstage; do
		pmc="$build/$stage/soc/intel/common/block/pmc/pmclib.o"
		test -s "$pmc"
		! nm --defined-only "$pmc" | grep -q \
			'intel_smm_invocation_private_cause\|pmc_read_smi_status'
	done
	objdump -d "$cause" | grep -Eq '[[:space:]]in[l]?[[:space:]]'
	! objdump -d "$cause" | grep -Eq '[[:space:]]out[l]?[[:space:]]'
done

grep -q '^smm-$(CONFIG_SMM_INVOCATION_INTEL_CAUSE) += invocation_cause.c$' \
	"$root/src/soc/intel/common/block/smm/Makefile.mk"
test "$(rg -l 'invocation_cause\.c' "$root/src" -g Makefile.mk | wc -l)" -eq 1

printf '%s\n' 'SMM invocation Intel private-cause profiles: PASS'
