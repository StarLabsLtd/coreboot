#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

scratch_make()
(
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES
	exec "${MAKE:-make}" VBOOT_SOURCE="$root/3rdparty/vboot" "$@"
)

test -f "$root/3rdparty/vboot/firmware/2lib/include/2api.h" || {
	echo 'vboot submodule is required for presence arm profiles' >&2
	exit 1
}

# Resolve the unmodified production Kconfig without forcing either dormant
# symbol.  Q35 covers the complete SMM archive; the MTL object target uses the
# production SMM compiler without requiring unrelated external FSP inputs.
for profile in emulation_qemu_x86_q35_smm_tseg starlabs_starbook_mtl; do
	case "$profile" in
		emulation*) name=q35 ;;
		*) name=mtl ;;
	esac
	config="$temporary/$name-natural.config"
	build="$temporary/$name-natural"
	cp "$root/configs/config.$profile" "$config"
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN -d LTO
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 DOTCONFIG="$config" \
		obj="$build" olddefconfig
	! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION=y$' "$config"
	! grep -q '^CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE=y$' "$config"
	if [ "$name" = q35 ]; then
		natural_targets="$build/smm/smm.a"
	else
		natural_targets="$build/smm/soc/intel/common/block/smm/smihandler.o"
	fi
	# Deliberate target-list splitting for the selected production artifact.
	# shellcheck disable=SC2086
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 DOTCONFIG="$config" \
		obj="$build" -B $natural_targets
	test ! -e "$build/smm/lib/payload_mm_authvar_presence_route_session.o"
	! find "$build/smm" -type f \( -name '*.o' -o -name '*.a' \) -print0 | \
		xargs -0 nm -A 2>/dev/null | \
		grep -q 'payload_mm_authvar_presence_route_session'
done

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
	select SMM_APMC_COMPOSITION_ATTESTED
	select SMM_APMC_COMMAND_REGISTRY

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES

config SMM_INVOCATION_INTEL_ADAPTER
	default y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES && SOC_INTEL_COMMON_BLOCK_SMM
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
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" \
	"$profile_kconfig"

# This fixture intentionally adds a conditional default-y with all
# dependencies satisfied.  It demonstrates why a forced-off-only profile is
# insufficient: olddefconfig enables both route symbols naturally, while
# explicit user n values mask the enabling Kconfig.
mask_config="$temporary/forced-off-mask.config"
mask_build="$temporary/forced-off-mask"
cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" "$mask_config"
"$root/util/scripts/config" --file "$mask_config" -e ANY_TOOLCHAIN \
	-e TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES -d LTO
scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
	KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$mask_config" \
	obj="$mask_build" olddefconfig
grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION=y$' "$mask_config"
"$root/util/scripts/config" --file "$mask_config" \
	-d PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION \
	-d SMM_APMC_ROUTE_AUTHVAR_PRESENCE
scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
	KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$mask_config" \
	obj="$mask_build" olddefconfig
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION=y$' "$mask_config"
! grep -q '^CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE=y$' "$mask_config"

for profile in emulation_qemu_x86_q35_smm_tseg starlabs_starbook_mtl; do
	case "$profile" in
		emulation*) name=q35 ;;
		*) name=mtl ;;
	esac
	config="$temporary/$name.config"
	build="$temporary/$name"
	cp "$root/configs/config.$profile" "$config"
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
		-e TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES -d LTO
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" \
		obj="$build" olddefconfig
	grep -q '^# CONFIG_LTO is not set$' "$config"
	grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ARM=y$' "$config"
	grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION=y$' "$config"
	if [ "$name" = q35 ]; then
		smm_targets="$build/smm/smm.a"
	else
		smm_targets="
			$build/smm/lib/payload_mm_authvar_presence_route_session.o
			$build/smm/lib/payload_mm_authvar_presence_arm.o
			$build/smm/lib/payload_mm_authvar_presence_transaction_receiver.o
			$build/smm/lib/payload_mm_authvar_presence_transaction.o
			$build/smm/lib/payload_mm_authvar_presence.o
			$build/smm/lib/bootmem_reservation_receipt.o
			$build/smm/cpu/x86/smm_command.o
			$build/smm/cpu/x86/smm_invocation_entry.o
			$build/smm/cpu/x86/smm_invocation_evidence.o
			$build/smm/cpu/x86/smm_invocation_evidence_loader.o
			$build/smm/cpu/x86/smm_invocation_loader_composition_gate.o
			$build/smm/cpu/x86/smm_invocation_loader_instance.o
			$build/smm/cpu/x86/smm_invocation_topology.o
			$build/smm/soc/intel/common/block/smm/invocation_adapter.o
			$build/smm/lib/memcmp.o
			$build/smm/arch/x86/memcpy.o
			$build/smm/arch/x86/memset.o"
	fi
	# Deliberate target-list splitting for the exact selected-object manifest.
	# shellcheck disable=SC2086
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig" DOTCONFIG="$config" obj="$build" \
		STACK_AUDIT_CFLAGS='-fstack-usage -fcallgraph-info=su -save-temps=obj' \
		-B $smm_targets
	if [ "$name" = q35 ]; then
		test -f "$build/smm/smm.a"
	fi
	file "$build/smm/lib/payload_mm_authvar_presence_arm.o" | grep -q 'ELF 32-bit'
	file "$build/smm/lib/payload_mm_authvar_presence_route_session.o" | \
		grep -q 'ELF 32-bit'
	! nm -u "$build/smm/lib/payload_mm_authvar_presence_arm.o" | \
		grep -Eq '__atomic|libatomic'
	! nm -u "$build/smm/lib/payload_mm_authvar_presence_route_session.o" | \
		grep -Eq '__atomic|libatomic'
	test -s "$build/xcompile"
	profile_cc=$(sed -n 's/^GCC_CC_x86_32:=//p' "$build/xcompile")
	test -n "$profile_cc"
	command -v "$profile_cc" >/dev/null
	if [ "$name" = q35 ]; then
		for member in \
			payload_mm_authvar_presence_route_session.o \
			payload_mm_authvar_presence_arm.o \
			payload_mm_authvar_presence_transaction_receiver.o \
			smm_invocation_evidence.o smm_invocation_entry.o \
			smm_invocation_loader_instance.o smm_invocation_topology.o; do
			ar t "$build/smm/smm.a" | grep -q "/$member$"
		done
	fi
	test "$(find "$build/smm" -name \
		'payload_mm_authvar_presence_route_session.ci' | wc -l)" -eq 1
	test "$(find "$build/smm" -name \
		'payload_mm_authvar_presence_route_session.su' | wc -l)" -eq 1
	find "$build/smm" -name '*.ci' -type f -print0 | sort -z | \
		xargs -0 cat > "$temporary/$name-smm.ci"
	graph_output=$(awk -v limit=12288 -v trace=1 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$temporary/$name-smm.ci")
	printf '%s: %s\n' "$name" "$graph_output"
	graph_max=$(printf '%s\n' "$graph_output" | awk '{ print $5 }')
	test "$graph_max" -gt 0
	! awk -v limit="$((graph_max - 1))" \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$temporary/$name-smm.ci" >/dev/null 2>&1
	awk '
		!changed && /sourcename: "payload_mm_authvar_presence_route_session_dispatch_locked"/ &&
		/targetname: "smm_apmc_command_consume"/ {
			print; changed = 1
		}
		{ print }
	' "$temporary/$name-smm.ci" > "$temporary/$name-duplicate-direct.ci"
	! awk -v limit=12288 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$temporary/$name-duplicate-direct.ci" >/dev/null 2>&1
	awk '
		!changed && /sourcename: "payload_mm_authvar_presence_route_session_dispatch_locked"/ &&
		/targetname: "smm_apmc_command_consume"/ {
			sub(/targetname: "smm_apmc_command_consume"/,
				"targetname: \"memcmp\""); changed = 1
		}
		{ print }
		END {
			print "edge: { sourcename: \"decoy\" targetname: \"smm_apmc_command_consume\" label: \"payload_mm_authvar_presence_route_session.c:1230:6\" }"
		}
	' "$temporary/$name-smm.ci" > "$temporary/$name-target-decoy.ci"
	! awk -v limit=12288 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$temporary/$name-target-decoy.ci" >/dev/null 2>&1
	awk '
		!changed && /payload_mm_authvar_presence_route_session.c:1230:6/ {
			sub(/1230:6/, "1230:7"); changed = 1
		}
		{ print }
	' "$temporary/$name-smm.ci" > "$temporary/$name-relocated-direct.ci"
	! awk -v limit=12288 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$temporary/$name-relocated-direct.ci" >/dev/null 2>&1
	cp "$temporary/$name-smm.ci" "$temporary/$name-extra-indirect.ci"
	printf '%s\n' \
		'edge: { sourcename: "payload_mm_authvar_presence_route_session_dispatch_locked" targetname: "__indirect_call" label: "payload_mm_authvar_presence_route_session.c:1:1" }' \
		>> "$temporary/$name-extra-indirect.ci"
	! awk -v limit=12288 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$temporary/$name-extra-indirect.ci" >/dev/null 2>&1
	awk '
		/payload_mm_authvar_presence_route_session_dispatch_locked\\n[0-9]+ bytes/ {
			sub(/[0-9]+ bytes/, "13000 bytes")
		}
		{ print }
	' "$temporary/$name-smm.ci" > "$temporary/$name-large-frame.ci"
	! awk -v limit=12288 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$temporary/$name-large-frame.ci" >/dev/null 2>&1
done

# A dormant route must not add an SMM object or symbol merely because all of
# its reviewed prerequisites are selected.  Historical byte identity against
# the post-evidence-prerequisite baseline is recorded as release evidence;
# this permanent gate checks the history-independent object-absence contract.
lib_kconfig_off="$temporary/lib-off.Kconfig"
sed '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION$/,/^config / {
	/default y if TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES/d
}' "$lib_kconfig" > "$lib_kconfig_off"
profile_kconfig_off="$temporary/Kconfig.off"
sed "s|source \"$lib_kconfig\"|source \"$lib_kconfig_off\"|" \
	"$profile_kconfig" > "$profile_kconfig_off"
for profile in emulation_qemu_x86_q35_smm_tseg starlabs_starbook_mtl; do
	case "$profile" in
		emulation*) name=q35 ;;
		*) name=mtl ;;
	esac
	config="$temporary/$name-off.config"
	build="$temporary/$name-off"
	cp "$root/configs/config.$profile" "$config"
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
		-e TEST_AUTHVAR_PRESENCE_ARM_PREREQUISITES \
		-d PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION \
		-d SMM_APMC_ROUTE_AUTHVAR_PRESENCE -d LTO
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig_off" DOTCONFIG="$config" \
		obj="$build" olddefconfig
	! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION=y$' "$config"
	! grep -q '^CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE=y$' "$config"
	if [ "$name" = q35 ]; then
		targets="$build/smm/smm.a"
	else
		targets="
			$build/smm/lib/payload_mm_authvar_presence_arm.o
			$build/smm/lib/payload_mm_authvar_presence_transaction_receiver.o
			$build/smm/cpu/x86/smm_invocation_evidence.o
			$build/smm/cpu/x86/smm_command.o
			$build/smm/soc/intel/common/block/smm/invocation_adapter.o"
	fi
	# Deliberate target-list splitting for the exact selected-object manifest.
	# shellcheck disable=SC2086
	scratch_make -s -C "$root" UPDATED_SUBMODULES=1 \
		KBUILD_KCONFIG="$profile_kconfig_off" DOTCONFIG="$config" \
		obj="$build" -B $targets
	test ! -e "$build/smm/lib/payload_mm_authvar_presence_route_session.o"
	! find "$build/smm" -type f \( -name '*.o' -o -name '*.a' \) -print0 | \
		xargs -0 nm -A 2>/dev/null | \
		grep -q 'payload_mm_authvar_presence_route_session'
done

printf '%s\n' 'Payload-MM presence arm Q35/MTL profiles: PASS'
