#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.logical-value-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
if [ -z "${LOGICAL_VALUE_KEEP_PROFILE:-}" ]; then
	trap 'rm -rf "$temporary"' EXIT HUP INT TERM
else
	printf 'logical-value profile directory: %s\n' "$temporary" >&2
fi
base=9f6cd512681341d3123c8be75ccaae9494dc5bb7

test -f "$root/3rdparty/vboot/firmware/include/vb2_sha.h"
obsolete_evidence='read_rax|write_rax|original_rax|read_registers|write_registers'
obsolete_presence='completion_rax|RAX_SENTINEL|transaction_rax|saved_rax'
if rg -q "$obsolete_evidence|$obsolete_presence|original_registers|smm_invocation_register_state" \
	"$root/src/include/cpu/x86/smm_invocation_evidence.h" \
	"$root/src/cpu/x86/smm_invocation_evidence.c" \
	"$root/src/include/boot/payload_mm_authvar_presence_producer.h" \
	"$root/src/include/boot/payload_mm_authvar_presence_route_session.h" \
	"$root/src/include/boot/payload_mm_authvar_presence_transaction.h" \
	"$root/src/lib/payload_mm_authvar_presence_arm.c" \
	"$root/src/lib/payload_mm_authvar_presence_producer.c" \
	"$root/src/lib/payload_mm_authvar_presence_route_session.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction.c" \
	"$root/src/lib/payload_mm_authvar_presence_transaction_receiver.c"; then
	printf '%s\n' 'logical-value stack retained an obsolete RAX-only API' >&2
	exit 1
fi
grep -q '^#define SMM_INVOCATION_EVIDENCE_REVISION 3U$' \
	"$root/src/include/cpu/x86/smm_invocation_evidence.h"
grep -q '^#define INTEL_SMM_INVOCATION_ADAPTER_REVISION 3U$' \
	"$root/src/include/cpu/intel/smm_invocation_adapter.h"
grep -q '^#define PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_REVISION 3U$' \
	"$root/src/include/boot/payload_mm_authvar_presence_producer.h"
grep -q '^#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION 4U$' \
	"$root/src/include/boot/payload_mm_authvar_presence_transaction.h"

baseline="$temporary/base"
mkdir -p "$baseline"
git -C "$root" archive "$base" | tar -x -C "$baseline"
rmdir "$baseline/3rdparty/vboot"
ln -s "$root/3rdparty/vboot" "$baseline/3rdparty/vboot"
ln -s "$root/../../intel_fsp" "$temporary/intel_fsp"

scratch_make()
(
	tree=$1
	shift
	vboot_source="$root/3rdparty/vboot"
	if [ "$tree" = "$baseline" ]; then
		vboot_source=3rdparty/vboot
	fi
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make VBOOT_SOURCE="$vboot_source" -C "$tree" "$@"
)

build_natural()
(
	tree=$1
	name=$2
	profile=$3
	build="$temporary/$name"
	config="$build/full.config"
	artifact=smm/smm

	mkdir -p "$build"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" \
		KBUILD_DEFCONFIG="configs/config.$profile" defconfig >/dev/null
	"$tree/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" olddefconfig >/dev/null
	! grep -q '^CONFIG_SMM_INVOCATION_EVIDENCE=y$' "$config"
	! grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER=y$' "$config"
	! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER=y$' "$config"
	! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION=y$' "$config"
	scratch_make "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" -j4 "$build/$artifact" >/dev/null
	test -s "$build/$artifact"
	test ! -e "$build/smm/cpu/x86/smm_invocation_evidence.o"
	test ! -e "$build/smm/soc/intel/common/block/smm/invocation_adapter.o"
	test ! -e "$build/smm/lib/payload_mm_authvar_presence_transaction.o"
)

for profile in emulation_qemu_x86_q35_smm_tseg starlabs_lite_glk \
	starlabs_lite_adl starlabs_starbook_mtl; do
	name=$(printf '%s' "$profile" | tr / _)
	build_natural "$root" "current-$name" "$profile"
	build_natural "$baseline" "base-$name" "$profile"
	artifact=smm/smm
	cmp "$temporary/current-$name/$artifact" \
		"$temporary/base-$name/$artifact"
	for object in cpu/x86/smm/smm_module_handler.o \
		cpu/x86/smm/smihandler.o \
		soc/intel/common/block/smm/smihandler.o; do
		current="$temporary/current-$name/smm/$object"
		old="$temporary/base-$name/smm/$object"
		if [ -e "$current" ] || [ -e "$old" ]; then
			test -e "$current" && test -e "$old"
			objcopy --strip-debug "$current" "$temporary/current-handler.o"
			objcopy --strip-debug "$old" "$temporary/base-handler.o"
			cmp "$temporary/current-handler.o" \
				"$temporary/base-handler.o"
		fi
	done
done

printf '%s\n' 'SMM invocation logical-value config-off identities: PASS'
