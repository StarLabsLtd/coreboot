#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.adapter-route-profile.XXXXXX")
temporary=$(CDPATH= cd -- "$temporary" && pwd -P)
if [ -z "${ADAPTER_ROUTE_KEEP_PROFILE:-}" ]; then
	trap 'rm -rf "$temporary"' EXIT HUP INT TERM
else
	printf 'adapter-route profile directory: %s\n' "$temporary" >&2
fi
vboot_source="$root/3rdparty/vboot"
if [ ! -f "$vboot_source/firmware/include/vb2_sha.h" ]; then
	vboot_source="$root/../../coreboot/3rdparty/vboot"
fi
test -f "$vboot_source/firmware/include/vb2_sha.h"

scratch_make()
(
	make_vboot_source=$vboot_source
	case " $* " in
	*" -C ${baseline:-/nonexistent} "*) make_vboot_source=3rdparty/vboot ;;
	esac
	unset MAKELEVEL MAKEFLAGS MFLAGS MAKEOVERRIDES GNUMAKEFLAGS
	exec make VBOOT_SOURCE="$make_vboot_source" "$@"
)

# The provider profile retains its transitive graph mutants, real native/STM
# geometry and exact feature-off identities.
if [ -z "${ADAPTER_ROUTE_SKIP_PROVIDER_PROFILE:-}" ]; then
	"$root/tests/cpu/x86/smm_invocation_adapter_provider_profiles_test.sh"
fi

profile_kconfig="$temporary/Kconfig.adapter-route-profile"
head -n 3 "$root/src/Kconfig" > "$profile_kconfig"
cat >> "$profile_kconfig" <<'EOF'

config TEST_MTL_ADAPTER_ROUTE_PREREQUISITES
	bool "test-only MTL adapter route prerequisites"
	default n
	select BOOTMEM_ALIGNED_RESERVATIONS
	select BOOTMEM_ALIGNED_RESERVATION_RECEIPT
	select ENABLE_EARLY_DMA_PROTECTION
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_FAIL_STOP
	select SMM_INVOCATION_EVIDENCE
	select SMM_INVOCATION_ENTRY_PLATFORM
	select SMM_INVOCATION_ENTRY
	select SMM_INVOCATION_INTEL_ADAPTER
	select SMM_INVOCATION_TOPOLOGY
	select SMM_INVOCATION_LOADER_INSTANCE
	select STARLABS_STARBOOK_MTL_SMM_INVOCATION_LOADER_INSTANCE_PROVIDER
	select SMM_INVOCATION_LOADER_COMPOSITION
	select SMM_INVOCATION_RUNTIME_VIEW
	select SMM_INVOCATION_INTEL_ADAPTER_PROVIDER
	select SMM_APMC_COMPOSITION_ATTESTED
	select SMM_APMC_COMMAND_REGISTRY

config TEST_MTL_ADAPTER_ROUTE_PROFILE
	bool "test-only MTL adapter route"
	default n
	select TEST_MTL_ADAPTER_ROUTE_PREREQUISITES
	select SMM_INVOCATION_INTEL_ADAPTER_ROUTE

config TEST_MTL_ADAPTER_ROUTE_STM_PROFILE
	bool "test-only MTL STM adapter route"
	default n
	select TEST_MTL_ADAPTER_ROUTE_PROFILE
	select ENABLE_VMX
	select STM

config SMM_MODULE_STACK_SIZE
	default 0x4000 if TEST_MTL_ADAPTER_ROUTE_PREREQUISITES
EOF
tail -n +4 "$root/src/Kconfig" >> "$profile_kconfig"

lib_kconfig="$temporary/lib.Kconfig"
sed -e 's/depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER$/'\
'depends on PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER || '\
'TEST_MTL_ADAPTER_ROUTE_PREREQUISITES/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_PUBLICATION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_ADAPTER_ROUTE_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_HANDOFF$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_ADAPTER_ROUTE_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_ADAPTER_ROUTE_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_PLATFORM$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_ADAPTER_ROUTE_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ARM$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_ADAPTER_ROUTE_PREREQUISITES\n\tdefault n/' \
	-e '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION$/,+2 '\
's/^\tdefault n$/\tdefault y if TEST_MTL_ADAPTER_ROUTE_PREREQUISITES\n\tdefault n/' \
	"$root/src/lib/Kconfig" > "$lib_kconfig"
sed -i "s|source \"src/lib/Kconfig\"|source \"$lib_kconfig\"|" \
	"$profile_kconfig"

for pair in MTL_ON:TEST_MTL_ADAPTER_ROUTE_PROFILE \
	MTL_STM_ON:TEST_MTL_ADAPTER_ROUTE_STM_PROFILE; do
	name=${pair%%:*}
	selector=${pair#*:}
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
	"$root/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
		-e "$selector" -d LTO
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		olddefconfig >/dev/null
	grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER_ROUTE=y$' "$config"
	grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER_PROVIDER=y$' "$config"
	grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION=y$' "$config"
	if [ "$name" = MTL_STM_ON ]; then
		grep -q '^CONFIG_STM=y$' "$config"
	else
		! grep -q '^CONFIG_STM=y$' "$config"
	fi
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		STACK_AUDIT_CFLAGS='-fstack-usage -fcallgraph-info=su' -j4 \
		"$build/smm/smm" >/dev/null

	object="$build/smm/soc/intel/common/block/smm/invocation_adapter_route.o"
test -f "$object"
file "$object" | grep -q 'ELF 32-bit'
test "$(nm --defined-only "$object" | awk \
	'$3 == "intel_smm_invocation_adapter_route_provision" { count++ }
	 END { print count + 0 }')" -eq 1
! nm -u "$object" | grep -Eq '__atomic_|__sync_'
nm -u "$object" | awk '{ print $2 }' | sort > "$temporary/undefined"
printf '%s\n' intel_smm_invocation_adapter_provider_provision \
	payload_mm_authvar_presence_route_session_provision | sort > \
	"$temporary/expected-undefined"
cmp "$temporary/expected-undefined" "$temporary/undefined"
	objdump -r "$object" | awk '
		/RELOCATION RECORDS FOR \[.text.intel_smm_invocation_adapter_route_provision\]/ {
			in_route = 1; next
		}
		in_route && /^$/ { exit }
		in_route && $2 == "R_386_PC32" { print $3 }
	' > "$temporary/call-order"
	printf '%s\n' intel_smm_invocation_adapter_provider_provision \
		payload_mm_authvar_presence_route_session_provision > \
		"$temporary/expected-call-order"
	cmp "$temporary/expected-call-order" "$temporary/call-order"

graph=$(find "$build/smm" -type f -name 'invocation_adapter_route.ci')
test -n "$graph"
frame=$(awk -v limit=96 \
	-f "$root/tests/cpu/x86/smm_invocation_adapter_route_graph.awk" "$graph")
case "$frame" in ''|*[!0-9]*) exit 1 ;; esac
stack_usage=${graph%.ci}.su
test -f "$stack_usage"
test "$(awk -F '\t' \
	'$1 ~ /intel_smm_invocation_adapter_route_provision$/ &&
	 $2 == 96 && $3 == "dynamic,bounded" { count++ }
	 END { print count + 0 }' "$stack_usage")" -eq 1
	combined_graph="$build/adapter-route-linked.ci"
	find "$build/smm" -type f -name '*.ci' -exec cat {} + > "$combined_graph"
	provider_bound=$(awk -v limit=1024 \
		-f "$root/tests/cpu/x86/smm_invocation_adapter_provider_stack_graph.awk" \
		"$combined_graph")
	route_report=$(awk -v limit=12288 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$combined_graph")
	route_bound=$(printf '%s\n' "$route_report" | awk '{ print $5 }')
	case "$provider_bound:$route_bound" in
	*[!0-9:]*) exit 1 ;;
	esac
	child_bound=$provider_bound
	[ "$route_bound" -le "$child_bound" ] || child_bound=$route_bound
	test "$((frame + child_bound))" -le 16384
	child_mutant="$build/adapter-route-linked-oversized-child.ci"
	sed '/title: "payload_mm_authvar_presence_route_session_provision"/ s/[0-9][0-9]* bytes/13000 bytes/' \
		"$combined_graph" > "$child_mutant"
	! awk -v limit=12288 \
		-f "$root/tests/lib/payload_mm_authvar_presence_route_stack_graph.awk" \
		"$child_mutant" >/dev/null 2>&1

for mutation in missing-provider missing-route extra-edge non-root smaller \
	unknown indirect recursive dynamic-only unbounded missing-class \
	unknown-class oversized; do
	mutant="$temporary/$mutation.ci"
	case "$mutation" in
	missing-provider)
		sed '/sourcename: "intel_smm_invocation_adapter_route_provision".*targetname: "intel_smm_invocation_adapter_provider_provision"/d' \
			"$graph" > "$mutant" ;;
	missing-route)
		sed '/sourcename: "intel_smm_invocation_adapter_route_provision".*targetname: "payload_mm_authvar_presence_route_session_provision"/d' \
			"$graph" > "$mutant" ;;
	extra-edge)
		cp "$graph" "$mutant"
		printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_route_provision" targetname: "intel_smm_invocation_adapter_provider_arm" label: "mutation:1:1" }' >> "$mutant" ;;
	non-root)
		cp "$graph" "$mutant"
		printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_provider_provision" targetname: "payload_mm_authvar_presence_route_session_prepare_lock_release" label: "mutation:1:1" }' >> "$mutant" ;;
	unknown)
		cp "$graph" "$mutant"
		printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_route_provision" targetname: "missing_route_target" label: "mutation:1:1" }' >> "$mutant" ;;
	indirect)
		cp "$graph" "$mutant"
		printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_route_provision" targetname: "__indirect_call" label: "mutation:1:1" }' >> "$mutant" ;;
	recursive)
		cp "$graph" "$mutant"
		printf '%s\n' 'edge: { sourcename: "intel_smm_invocation_adapter_route_provision" targetname: "intel_smm_invocation_adapter_route_provision" label: "mutation:1:1" }' >> "$mutant" ;;
	smaller)
		sed '/title: "intel_smm_invocation_adapter_route_provision"/ s/96 bytes/80 bytes/' \
			"$graph" > "$mutant" ;;
	dynamic-only)
		sed '/title: "intel_smm_invocation_adapter_route_provision"/ s/dynamic,bounded/dynamic/' \
			"$graph" > "$mutant" ;;
	unbounded)
		sed '/title: "intel_smm_invocation_adapter_route_provision"/ s/dynamic,bounded/dynamic,unbounded/' \
			"$graph" > "$mutant" ;;
	missing-class)
		sed '/title: "intel_smm_invocation_adapter_route_provision"/ s/ (dynamic,bounded)//' \
			"$graph" > "$mutant" ;;
	unknown-class)
		sed '/title: "intel_smm_invocation_adapter_route_provision"/ s/dynamic,bounded/unknown/' \
			"$graph" > "$mutant" ;;
	oversized)
		sed '/label: "intel_smm_invocation_adapter_route_provision\\n/ s/[0-9][0-9]* bytes/129 bytes/' \
			"$graph" > "$mutant" ;;
	esac
	! awk -v limit=96 \
		-f "$root/tests/cpu/x86/smm_invocation_adapter_route_graph.awk" \
		"$mutant" >/dev/null 2>&1
done

# The wrapper adds exactly 96 bounded bytes to either independently bounded child.
# The linked child graphs are independently resolved above, so their actual
# maximum plus the exact wrapper frame remains below the selected 16 KiB stack.
test "$((frame + child_bound))" -le 16384
	test "$(find "$build/smm" -type f -name 'invocation_adapter_route.o' | wc -l)" -eq 1
	linked_count=$(nm --defined-only "$build/smm/smm.elf" | awk \
		'$3 == "intel_smm_invocation_adapter_route_provision" { count++ }
		 END { print count + 0 }')
	case "$linked_count" in
	0) printf '%s: dormant route object built once; linked section GC removed\n' \
		"$name" ;;
	1) printf '%s: dormant route symbol retained exactly once\n' "$name" ;;
	*) exit 1 ;;
	esac
	find "$build" -path "$build/smm" -prune -o -type f -name '*.o' \
		-exec nm --defined-only {} + 2>/dev/null | \
		awk '$NF == "intel_smm_invocation_adapter_route_provision" { found = 1 }
		END { exit found }'
done

# Compare the same synthetic MTL provider plus route-session composition with
# the route symbol disabled against exact PR263. The linked SMM must remain
# identical even though the later logical-value API migration intentionally
# changes the dormant provider and route-session input objects.
baseline="$temporary/base"
mkdir -p "$baseline"
git -C "$root" archive 38fba614b80ac7d26a6b7fbedf94e7e98e4d62a2 | \
	tar -x -C "$baseline"
rmdir "$baseline/3rdparty/vboot" "$baseline/3rdparty/stm"
ln -s "$vboot_source" "$baseline/3rdparty/vboot"
ln -s "$root/../../coreboot/3rdparty/stm" "$baseline/3rdparty/stm"
ln -s "$root/../../intel_fsp" "$temporary/intel_fsp"

build_route_off()
{
	tree=$1
	name=$2
	build="$temporary/$name"
	config="$build/full.config"
	mkdir -p "$build"
	scratch_make -C "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		KBUILD_DEFCONFIG=configs/config.starlabs_starbook_mtl defconfig >/dev/null
	"$tree/util/scripts/config" --file "$config" -e ANY_TOOLCHAIN \
		-e TEST_MTL_ADAPTER_ROUTE_PREREQUISITES \
		-d SMM_INVOCATION_INTEL_ADAPTER_ROUTE -d LTO
	scratch_make -C "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" \
		olddefconfig >/dev/null
	grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER_PROVIDER=y$' "$config"
	grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_ROUTE_SESSION=y$' "$config"
	! grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER_ROUTE=y$' "$config"
	scratch_make -C "$tree" UPDATED_SUBMODULES=1 obj="$build" \
		DOTCONFIG="$config" KBUILD_KCONFIG="$profile_kconfig" -j4 \
		"$build/smm/smm" >/dev/null
}

build_route_off "$root" CURRENT_ROUTE_OFF
build_route_off "$baseline" BASE_ROUTE_OFF
cmp "$temporary/CURRENT_ROUTE_OFF/smm/smm" \
	"$temporary/BASE_ROUTE_OFF/smm/smm"
for object in \
	smm/soc/intel/common/block/smm/invocation_adapter_provider.o \
	smm/lib/payload_mm_authvar_presence_route_session.o; do
	test -f "$temporary/CURRENT_ROUTE_OFF/$object"
done
test ! -e "$temporary/CURRENT_ROUTE_OFF/smm/soc/intel/common/block/smm/invocation_adapter_route.o"
! nm --defined-only "$temporary/CURRENT_ROUTE_OFF/smm/smm.elf" | \
	grep -q 'intel_smm_invocation_adapter_route_provision'

# Natural profiles must not select or emit the dormant route object.
for profile in emulation_qemu_x86_q35_smm_tseg starlabs_lite_glk \
	starlabs_lite_adl starlabs_starbook_mtl; do
	name=$(printf '%s' "$profile" | tr / _)
	natural="$temporary/$name"
	natural_config="$natural/full.config"
	mkdir -p "$natural"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$natural" \
		DOTCONFIG="$natural_config" \
		KBUILD_DEFCONFIG="configs/config.$profile" defconfig >/dev/null
	"$root/util/scripts/config" --file "$natural_config" -e ANY_TOOLCHAIN
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$natural" \
		DOTCONFIG="$natural_config" olddefconfig >/dev/null
	! grep -q '^CONFIG_SMM_INVOCATION_INTEL_ADAPTER_ROUTE=y$' "$natural_config"
	scratch_make -C "$root" UPDATED_SUBMODULES=1 obj="$natural" \
		DOTCONFIG="$natural_config" -j4 "$natural/smm/smm" >/dev/null
	test ! -e "$natural/smm/soc/intel/common/block/smm/invocation_adapter_route.o"
done

printf '%s\n' "Intel SMM invocation adapter route profiles: PASS ($frame-byte frame)"
