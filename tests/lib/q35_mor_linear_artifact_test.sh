#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/q35-mor-linear-artifact.XXXXXX")
cleanup()
{
	if [ -n "${KEEP_TEST_OUTPUT:-}" ]; then
		printf 'Q35 MOR artifact output retained at %s\n' "$temporary" >&2
	else
		rm -rf "$temporary"
	fi
}
trap cleanup EXIT HUP INT TERM

crossgcc=${XGCCPATH:-"$root/util/crossgcc/xgcc/bin"}
crossgcc=$(realpath "$crossgcc")
test -x "$crossgcc/i386-elf-gcc"
PATH="$crossgcc:$PATH"
export PATH

provider_kconfig=$root/src/mainboard/emulation/qemu-q35/Kconfig
provider_block=$temporary/provider.kconfig
sed -n '/^config Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER$/,/^config /p' \
	"$provider_kconfig" | sed '$d' > "$provider_block"

provider_kconfig_valid()
{
	block=$1
	test -s "$block" &&
		grep -qx '[[:space:]]*bool' "$block" &&
		grep -qx '[[:space:]]*default n' "$block" &&
		grep -qx '[[:space:]]*depends on HAVE_SMI_HANDLER' "$block" &&
		grep -qx '[[:space:]]*depends on SMM_MODULE_STACK_SIZE >= 0x4000' \
			"$block" &&
		grep -q 'depends on .*\(!HAVE_ACPI_RESUME\)' "$block" &&
		grep -q 'depends on .*\(!SMMSTORE\)' "$block" &&
		grep -qx '[[:space:]]*depends on PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR' \
			"$block" &&
		! grep -q 'bool "' "$block" &&
		! grep -q 'select PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR' "$block"
}

provider_kconfig_valid "$provider_block"
for dependency in HAVE_SMI_HANDLER 'SMM_MODULE_STACK_SIZE >= 0x4000' \
	'!HAVE_ACPI_RESUME' '!SMMSTORE'; do
	mutant=$temporary/provider-without-$(printf '%s' "$dependency" | tr -cd 'A-Za-z0-9_').kconfig
	sed "s/$dependency/BROKEN_DEPENDENCY/" "$provider_block" > "$mutant"
	if provider_kconfig_valid "$mutant"; then
		printf 'ERROR: missing %s provider dependency survived\n' \
			"$dependency" >&2
		exit 1
	fi
done
selector_mutant=$temporary/provider-direct-selector.kconfig
sed 's/depends on PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR/select PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR/' \
	"$provider_block" > "$selector_mutant"
if provider_kconfig_valid "$selector_mutant"; then
	printf '%s\n' 'ERROR: direct orchestrator selection survived' >&2
	exit 1
fi

prepare_config()
{
	name=$1
	config=$temporary/config-$name
	build=$temporary/build-$name
	mkdir -p "$config"
	cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" \
		"$config/.config"
	if [ "$name" = on ]; then
		sed -i '/^CONFIG_SMM_MODULE_STACK_SIZE=/d' "$config/.config"
		printf '%s\n' 'CONFIG_SMM_MODULE_STACK_SIZE=0x4000' >> \
			"$config/.config"
		cp "$root/src/Kconfig" "$config/Kconfig"
		cat >> "$config/Kconfig" <<'EOF'

config TEST_Q35_MOR_LINEAR_ARTIFACT
	bool
	default y
	select Q35_PAYLOAD_MM_AUTHVAR_EXECUTOR_TEST_PROOF
	select Q35_PAYLOAD_MM_MOR_TEST_ADAPTER
	select PAYLOAD_RESOURCE_HANDOFF
	select Q35_VTD_DMA_TEST_BACKEND
	select BOOTMEM_ALIGNED_RESERVATIONS
	select BOOTMEM_ALIGNED_RESERVATION_RECEIPT
	select PAYLOAD_MM_AUTHVAR_MOR_POLICY
	select PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE
	select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_SEAL
	select PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER
	select PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION
	select PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP
	select PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI
	select PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR
	select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND
	select PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR
	select PAYLOAD_MM_AUTHVAR_MOR_EARLY_DISCOVERY
	select Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER

config SMM_MODULE_STACK_SIZE
	range 0x4000 0x4000 if TEST_Q35_MOR_LINEAR_ARTIFACT
EOF
		kconfig=$config/Kconfig
	else
		kconfig=$root/src/Kconfig
	fi
	make -C "$root" obj="$build" KBUILD_KCONFIG="$kconfig" \
		DOTCONFIG="$config/.config" olddefconfig >/dev/null
}

prepare_config off
prepare_config on

for symbol in Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER \
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR \
	PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP \
	PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI \
	PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY \
	PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND \
	Q35_VTD_DMA_TEST_BACKEND; do
	! grep -qx "CONFIG_${symbol}=y" "$temporary/config-off/.config"
	grep -qx "CONFIG_${symbol}=y" "$temporary/config-on/.config"
done
grep -qx 'CONFIG_CPU_QEMU_X86_TSEG_SMM=y' "$temporary/config-on/.config"
grep -qx 'CONFIG_SMM_MODULE_STACK_SIZE=0x4000' \
	"$temporary/config-on/.config"
grep -qx 'CONFIG_SMM_MODULE_STACK_SIZE=0x400' \
	"$temporary/config-off/.config"
grep -qx '# CONFIG_SMMSTORE is not set' "$temporary/config-on/.config"
! grep -qx 'CONFIG_HAVE_ACPI_RESUME=y' "$temporary/config-on/.config"
grep -qx '# CONFIG_LTO is not set' "$temporary/config-on/.config"

build_config()
{
	name=$1
	config=$temporary/config-$name
	build=$temporary/build-$name
	if [ "$name" = on ]; then
		kconfig=$config/Kconfig
	else
		kconfig=$root/src/Kconfig
	fi
	stack_flags='-fstack-usage -fcallgraph-info=su -save-temps=obj'
	make -C "$root" -j4 obj="$build" KBUILD_KCONFIG="$kconfig" \
		DOTCONFIG="$config/.config" STACK_AUDIT_CFLAGS="$stack_flags" \
		"$build/coreboot.rom" >"$temporary/build-$name.log" 2>&1 || {
		cat "$temporary/build-$name.log" >&2
		exit 1
	}
}

build_config off
build_config on

xcompile=$temporary/build-on/xcompile
cross_cc=$(awk -F ':=' '$1 == "GCC_CC_x86_32" {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2); print $2; exit
}' "$xcompile")
cross_nm=$(awk -F ':=' '$1 ~ /^[[:space:]]*NM_x86_32$/ {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2); print $2; exit
}' "$xcompile")
cross_objdump=$(awk -F ':=' '$1 ~ /^[[:space:]]*OBJDUMP_x86_32$/ {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2); print $2; exit
}' "$xcompile")
cross_cc=$(command -v "$cross_cc")
cross_nm=$(command -v "$cross_nm")
cross_objdump=$(command -v "$cross_objdump")
for tool in "$cross_cc" "$cross_nm" "$cross_objdump"; do
	case $(realpath "$tool") in
	"$crossgcc"/*) ;;
	*)
		printf 'ERROR: build selected unpinned tool: %s\n' "$tool" >&2
		exit 1
		;;
	esac
done

off_ramstage=$temporary/build-off/cbfs/fallback/ramstage.debug
on_ramstage=$temporary/build-on/cbfs/fallback/ramstage.debug
off_smm=$temporary/build-off/smm/smm.elf
on_smm=$temporary/build-on/smm/smm.elf
for artifact in "$temporary/build-off/coreboot.rom" \
	"$temporary/build-on/coreboot.rom" "$off_ramstage" "$on_ramstage" \
	"$off_smm" "$on_smm"; do
	test -s "$artifact"
done

off_symbols=$temporary/off.symbols
on_symbols=$temporary/on.symbols
off_smm_symbols=$temporary/off-smm.symbols
smm_symbols=$temporary/smm.symbols
"$cross_nm" "$off_ramstage" > "$off_symbols"
"$cross_nm" "$on_ramstage" > "$on_symbols"
"$cross_nm" "$off_smm" > "$off_smm_symbols"
"$cross_nm" "$on_smm" > "$smm_symbols"

symbol_strong_present()
{
	symbol=$1
	file=$2
	awk -v symbol="$symbol" '$3 == symbol {
		found++
		if ($2 != "T" && $2 != "t") bad = 1
	}
		END { exit found != 1 || bad }' "$file"
}

symbol_absent()
{
	symbol=$1
	file=$2
	! awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$file"
}

ramstage_symbols='platform_payload_mm_authvar_mor_linear_ops
platform_payload_mm_authvar_smm_arena_seed
platform_payload_mm_authvar_smm_arena_required
platform_payload_mm_authvar_smm_arena_abort
platform_payload_mm_authvar_mor_private_smi_seed
platform_payload_mm_authvar_mor_private_smi_required
payload_mm_authvar_mor_live_inventory_compose_owned
q35_mor_dma_pre_device_guard_valid
q35_mor_dma_snapshot
q35_vtd_switch_root'
printf '%s\n' "$ramstage_symbols" | while IFS= read -r symbol; do
	test -n "$symbol"
	symbol_absent "$symbol" "$off_symbols"
	symbol_strong_present "$symbol" "$on_symbols"
	symbol_absent "$symbol" "$smm_symbols"
done

smm_platform_symbol=platform_payload_mm_authvar_mor_private_smi_bootstrap
symbol_absent "$smm_platform_symbol" "$off_symbols"
symbol_absent "$smm_platform_symbol" "$off_smm_symbols"
symbol_absent "$smm_platform_symbol" "$on_symbols"
symbol_strong_present "$smm_platform_symbol" "$smm_symbols"
for obsolete in payload_mm_authvar_mor_live_inventory_compose \
	payload_mm_authvar_mor_private_smi_close_unused_checked; do
	symbol_absent "$obsolete" "$on_symbols"
	symbol_absent "$obsolete" "$smm_symbols"
done

# The provider owns its scratch rather than recreating either removed API or
# the test-only convenience composer in production.
provider=$root/src/mainboard/emulation/qemu-q35/mor_platform.c
provider_api_valid()
{
	source=$1
	grep -q 'payload_mm_authvar_mor_live_inventory_compose_owned(' "$source" &&
		! grep -q 'payload_mm_authvar_mor_live_inventory_compose(' "$source" &&
		grep -q 'payload_mm_authvar_mor_private_smi_close_unused(' "$source" &&
		! grep -q 'payload_mm_authvar_mor_private_smi_close_unused_checked' "$source"
}

provider_api_valid "$provider"
compose_mutant=$temporary/provider-obsolete-compose.c
sed 's/payload_mm_authvar_mor_live_inventory_compose_owned/payload_mm_authvar_mor_live_inventory_compose/' \
	"$provider" > "$compose_mutant"
if provider_api_valid "$compose_mutant"; then
	printf '%s\n' 'ERROR: obsolete inventory composer survived' >&2
	exit 1
fi
close_mutant=$temporary/provider-obsolete-close.c
sed 's/payload_mm_authvar_mor_private_smi_close_unused(/payload_mm_authvar_mor_private_smi_close_unused_checked(/' \
	"$provider" > "$close_mutant"
if provider_api_valid "$close_mutant"; then
	printf '%s\n' 'ERROR: obsolete private-close API survived' >&2
	exit 1
fi

# Bound every Q35 provider frame and its aggregate object-local frame set. The
# generic coordinator/executor path has its own complete call-graph gate; this
# selected-image gate caps the additional platform callback contribution to a
# quarter of ramstage and rejects dynamic/unbounded frames.
provider_su=$(find "$temporary/build-on/ramstage" -type f \
	-path '*/mainboard/emulation/qemu-q35/mor_platform.su' -print)
test "$(printf '%s\n' "$provider_su" | sed '/^$/d' | wc -l)" -eq 1
! grep -q 'dynamic' "$provider_su"
stack_size=$(awk '$1 == "#define" && $2 == "CONFIG_STACK_SIZE" {
	print $3; found++
}
	END { if (found != 1) exit 1 }' "$temporary/build-on/config.h")
provider_limit=$((stack_size / 4))
test "$provider_limit" -gt 0
provider_total=$(awk '{ total += $2; if ($2 > largest) largest = $2 }
	END { if (!NR) exit 1; print total + 0, largest + 0 }' "$provider_su")
provider_sum=${provider_total% *}
provider_peak=${provider_total#* }
test "$provider_peak" -le "$provider_limit"
test "$provider_sum" -le "$provider_limit"

# Compute the maximum complete selected bootstrap call path from the exact SMM
# compiler graph. The graph rejects missing frames, recursion, unbounded frames
# and every indirect site without an explicit selected callback binding.
smm_stack=$(awk '$1 == "#define" && $2 == "CONFIG_SMM_MODULE_STACK_SIZE" {
	print $3; found++
}
	END { if (found != 1) exit 1 }' "$temporary/build-on/config.h")
smm_reserve=512
smm_graph=$temporary/smm-stack.ci
find "$temporary/build-on/smm" -type f -name '*.ci' -exec cat {} + > \
	"$smm_graph"
test -s "$smm_graph"

# Runtime objects do not carry the selected build's .su/.ci flags. Measure each
# linked helper's exact prologue, rejecting calls, outbound/indirect branches,
# and unrecognised ESP manipulation. The sole stale pre-link edge is accepted
# only when nm proves that helper absent from the exact selected SMM ELF.
runtime_frame()
{
	symbol=$1
	disassembly=$temporary/$symbol-smm.dis
	"$cross_objdump" -d --disassemble="$symbol" "$on_smm" > "$disassembly"
	locals_hex=$(sed -n 's/^.*sub[[:space:]]\+\$\(0x[0-9a-f]\+\),%esp.*$/\1/p' \
		"$disassembly")
	test -n "$locals_hex"
	test "$(awk '/^[[:space:]]*[0-9a-f]+:.*push[a-z]*([[:space:]]|$)/ { count++ }
		END { print count + 0 }' "$disassembly")" -eq 4
	test "$(grep -Ec 'sub[[:space:]]+.*,%esp' "$disassembly")" -eq 1
	! grep -Eq '[[:space:]](call|enter|pusha|leave|iret)[a-z]*([[:space:]]|$)' \
		"$disassembly"
	! awk -v locals="$locals_hex" '/^[[:space:]]*[0-9a-f]+:/ && /,%esp([[:space:]]|$)/ {
		if (($0 ~ /sub[[:space:]]+\$/ || $0 ~ /add[[:space:]]+\$/) &&
		    index($0, "$" locals ",%esp") != 0)
			next
		bad = 1
	}
		END { exit !bad }' "$disassembly"
	set -- $("$cross_nm" -S "$on_smm" | awk -v symbol="$symbol" \
		'$4 == symbol { print $1, $2; found++ }
		 END { if (found != 1) exit 1 }')
	start=$((0x$1))
	end=$((start + 0x$2))
	branch_count=$(grep -Ec '^[[:space:]]*[0-9a-f]+:.*[[:space:]](j[a-z]*|ljmp|loop[a-z]*)([[:space:]]|$)' \
		"$disassembly")
	branch_targets=$(sed -n '/^[[:space:]]*[0-9a-f]\+:.*[[:space:]]\(j[a-z]*\|ljmp\|loop[a-z]*\)[[:space:]]/ {
		s/^.*[[:space:]]\([0-9a-f][0-9a-f]*\)[[:space:]]<.*$/\1/p
	}' "$disassembly")
	test "$(printf '%s\n' "$branch_targets" | sed '/^$/d' | wc -l)" -eq \
		"$branch_count"
	for target_hex in $branch_targets; do
		target=$((0x$target_hex))
		test "$target" -ge "$start" && test "$target" -lt "$end"
	done
	locals=$((locals_hex))
	printf '%u\n' $((((locals + 16 + 15) / 16) * 16))
}

runtime_udiv_bytes=$(runtime_frame __udivdi3)
runtime_udivmod_bytes=$(runtime_frame __udivmoddi4)
runtime_umod_bytes=$(runtime_frame __umoddi3)
symbol_absent __divdi3 "$smm_symbols"
graph_args="-v limit=$((smm_stack - smm_reserve)) -v strict_sites=1 -v runtime_div_absent=1
-v runtime_udiv_bytes=$runtime_udiv_bytes
-v runtime_udivmod_bytes=$runtime_udivmod_bytes
-v runtime_umod_bytes=$runtime_umod_bytes"
smm_bound=$(awk $graph_args \
	-f "$root/tests/lib/q35_mor_smm_stack_graph.awk" "$smm_graph")
case $smm_bound in
''|*[!0-9]*) exit 1 ;;
esac

missing_edge=$temporary/smm-stack-missing-edge.ci
sed '/bootstrap_receive.*platform_payload_mm_authvar_mor_private_smi_bootstrap/d' \
	"$smm_graph" > "$missing_edge"
! cmp -s "$smm_graph" "$missing_edge"
! awk $graph_args \
	-f "$root/tests/lib/q35_mor_smm_stack_graph.awk" "$missing_edge" \
	>/dev/null 2>&1

missing_entry_edge=$temporary/smm-stack-missing-entry-edge.ci
sed '/smm_handler_start.*payload_mm_authvar_mor_private_smi_dispatch/d' \
	"$smm_graph" > "$missing_entry_edge"
! cmp -s "$smm_graph" "$missing_entry_edge"
! awk $graph_args -f "$root/tests/lib/q35_mor_smm_stack_graph.awk" \
	"$missing_entry_edge" >/dev/null 2>&1

missing_dispatch_edge=$temporary/smm-stack-missing-dispatch-edge.ci
sed '/payload_mm_authvar_mor_private_smi_dispatch.*bootstrap_receive/d' \
	"$smm_graph" > "$missing_dispatch_edge"
! cmp -s "$smm_graph" "$missing_dispatch_edge"
! awk $graph_args -f "$root/tests/lib/q35_mor_smm_stack_graph.awk" \
	"$missing_dispatch_edge" >/dev/null 2>&1

deep_edge=$temporary/smm-stack-deep-edge.ci
cp "$smm_graph" "$deep_edge"
printf '%s\n' \
	'node: { title: "q35_mor_stack_mutant" label: "q35_mor_stack_mutant\nmutation:1:1\n16384 bytes (static)" }' \
	'edge: { sourcename: "payload_mm_authvar_smm_bootstrap_install" targetname: "q35_mor_stack_mutant" label: "mutation:1:1" }' \
	>> "$deep_edge"
! awk $graph_args \
	-f "$root/tests/lib/q35_mor_smm_stack_graph.awk" "$deep_edge" \
	>/dev/null 2>&1

indirect_edge=$temporary/smm-stack-indirect-edge.ci
cp "$smm_graph" "$indirect_edge"
printf '%s\n' \
	'edge: { sourcename: "src/lib/payload_mm_authvar_mor_private_smi_receiver.c:bootstrap_receive" targetname: "__indirect_call" label: "mutation:1:1" }' \
	>> "$indirect_edge"
! awk $graph_args \
	-f "$root/tests/lib/q35_mor_smm_stack_graph.awk" "$indirect_edge" \
	>/dev/null 2>&1

printf 'selected coreboot toolchain: %s\n' "$crossgcc"
printf 'Q35 provider stack frames: peak %u, aggregate %u / %u bytes (%u-byte ramstage stack)\n' \
	"$provider_peak" "$provider_sum" "$provider_limit" "$stack_size"
printf 'Q35 rooted SMM bootstrap: %u + %u reserve / %u bytes\n' \
	"$smm_bound" "$smm_reserve" "$smm_stack"
"$root/tests/lib/q35_mor_qemu_test.sh" "$temporary/build-on/coreboot.rom" \
	"$temporary/qemu"
printf '%s\n' 'Q35 MOR selected/off artifact validation: PASS'
