#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
config="$temporary/config"
build="$temporary/build"
selected_ci="$temporary/selected.ci"
contract_ci="$temporary/contract.ci"
mkdir -p "$config"

cp "$root/configs/config.starlabs_starbook_mtl" "$config/.config"
cp "$root/src/Kconfig" "$config/Kconfig"
printf '%s\n' '' 'config TEST_MTL_MOR_STACK_SELECTOR' >> "$config/Kconfig"
printf '\t%s\n' 'bool' 'default y' \
	'select ENABLE_EARLY_DMA_PROTECTION' \
	'select STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING' \
	'select PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE' \
	'select PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR' >> "$config/Kconfig"
make -C "$root" obj="$build" KBUILD_KCONFIG="$config/Kconfig" \
	DOTCONFIG="$config/.config" olddefconfig >/dev/null
for symbol in BOARD_STARLABS_STARBOOK_MTL \
	STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING \
	PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE \
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR; do
	grep -qx "CONFIG_${symbol}=y" "$config/.config"
done

stack_flags='-fstack-usage -fcallgraph-info=su -fdump-ipa-cgraph -save-temps=obj'
make -C "$root" -j4 obj="$build" KBUILD_KCONFIG="$config/Kconfig" \
	DOTCONFIG="$config/.config" STACK_AUDIT_CFLAGS="$stack_flags" \
	"$build/cbfs/fallback/ramstage.debug" >"$temporary/build.log" 2>&1 || {
	cat "$temporary/build.log" >&2
	exit 1
}
cross_cc=$(awk -F ':=' '$1 == "GCC_CC_x86_32" { print $2; exit }' "$build/xcompile")
cross_objdump=$(awk -F ':=' '$1 == "OBJDUMP_x86_32" { print $2; exit }' "$build/xcompile")
cross_nm=$(awk -F ':=' '$1 ~ /^[[:space:]]*NM_x86_32$/ { print $2; exit }' "$build/xcompile")
cross_cc=$(command -v "$cross_cc")
cross_objdump=$(command -v "$cross_objdump")
cross_nm=$(command -v "$cross_nm")
printf 'selected coreboot toolchain: %s; %s; %s\n' \
	"$cross_cc" "$cross_objdump" "$cross_nm"
find "$build/ramstage" -name '*.ci' -exec cat {} + > "$selected_ci"
test -s "$selected_ci"
grep -qx '# CONFIG_LTO is not set' "$config/.config"
printf '%s\n' 'selected config: LTO disabled; using final per-object compiler artifacts'

private_callback=$(awk '
	$1 == "#define" &&
	$2 == "STARBOOK_MTL_MOR_PRIVATE_CALLBACK_STACK_MAX" {
		gsub(/U$/, "", $3)
		print $3
		found++
	}
	END { if (found != 1) exit 1 }
' "$root/src/mainboard/starlabs/starbook/variants/mtl/mor_platform.h")
test "$private_callback" -eq 1024
stack_size=$(awk '
	$1 == "#define" && $2 == "CONFIG_STACK_SIZE" { print $3; found++ }
	END { if (found != 1) exit 1 }
' "$build/config.h")
# Audit policy: MOR may consume at most half of the selected ramstage stack;
# the other half remains for its boot-state caller and asynchronous firmware
# context. Derive both halves from the selected CONFIG_STACK_SIZE.
test $((stack_size % 2)) -eq 0
stack_reserve=$((stack_size / 2))
stack_limit=$((stack_size - stack_reserve))
test "$stack_limit" -gt 0
printf 'selected ramstage stack budget: %u - %u reserve = %u bytes\n' \
	"$stack_size" "$stack_reserve" "$stack_limit"

graph="$root/tests/lib/starbook_mtl_mor_stack_graph.awk"

# GCC runtime helpers have no compiler-produced .su file. Derive their caps
# from the exact selected ELF prologue and reject calls or an ABI/prologue drift.
runtime_frame()
{
	symbol=$1
	disassembly="$temporary/$symbol.dis"
	"$cross_objdump" -d --disassemble="$symbol" \
		"$build/cbfs/fallback/ramstage.debug" > "$disassembly"
	locals_hex=$(sed -n 's/^.*sub[[:space:]]\+\$\(0x[0-9a-f]\+\),%esp.*$/\1/p' \
		"$disassembly")
	test -n "$locals_hex"
	runtime_disassembly_frame "$disassembly" "$symbol" "$locals_hex"
}

runtime_disassembly_frame()
{
	disassembly=$1
	symbol=$2
	locals_hex=$3
	test "$(awk '/^[[:space:]]*[0-9a-f]+:.*push[a-z]*([[:space:]]|$)/ { count++ }
		END { print count + 0 }' "$disassembly")" -eq 4 || return 1
	test "$(awk '/^[[:space:]]*[0-9a-f]+:.*push[a-z]*[[:space:]]+%/ { count++ }
		END { print count + 0 }' "$disassembly")" -eq 4 || return 1
	test "$(grep -Ec 'sub[[:space:]]+.*,%esp' "$disassembly")" -eq 1 || return 1
	grep -Eq 'sub[[:space:]]+\$'"$locals_hex"',%esp' "$disassembly" || return 1
	! grep -Eq '[[:space:]](call|enter|pusha|leave|iret)[a-z]*([[:space:]]|$)|[[:space:]](inc|dec|pop|push)[a-z]*[[:space:]]+%esp|[[:space:]]xchg[a-z]*[[:space:]]+.*%esp' \
		"$disassembly" || return 1
	# The selected helpers may allocate and restore exactly their audited local
	# frame. Reject every other instruction with ESP as its destination.
	! awk -v locals="$locals_hex" '/^[[:space:]]*[0-9a-f]+:/ && /,%esp([[:space:]]|$)/ {
		if ($0 ~ /sub[[:space:]]+\$/ && index($0, "$" locals ",%esp") != 0)
			next
		if ($0 ~ /add[[:space:]]+\$/ && index($0, "$" locals ",%esp") != 0)
			next
		bad = 1
	}
		END { exit !bad }' "$disassembly" || return 1
	# Every branch must resolve directly inside this helper. This excludes
	# indirect tail transfers as well as direct or conditional outbound edges.
	! awk -v symbol="$symbol" '/^[[:space:]]*[0-9a-f]+:.*[[:space:]](j[a-z]*|ljmp|loop[a-z]*)([[:space:]]|$)/ {
		if ($0 !~ /</ || index($0, "<" symbol "+") == 0)
			bad = 1
	}
		END { exit !bad }' "$disassembly" || return 1
	locals=$((locals_hex))
	printf '%u\n' $((((locals + 16 + 15) / 16) * 16))
}
runtime_div_bytes=$(runtime_frame __divdi3)
runtime_udiv_bytes=$(runtime_frame __udivdi3)
runtime_udivmod_bytes=$(runtime_frame __udivmoddi4)
runtime_umod_bytes=$(runtime_frame __umoddi3)
runtime_mutant="$temporary/runtime-second-decrement.dis"
cp "$temporary/__divdi3.dis" "$runtime_mutant"
printf '%s\n' '  0: 83 ec 04 sub $0x4,%esp' >> "$runtime_mutant"
runtime_div_locals=$(sed -n 's/^.*sub[[:space:]]\+\$\(0x[0-9a-f]\+\),%esp.*$/\1/p' \
	"$temporary/__divdi3.dis")
if runtime_disassembly_frame "$runtime_mutant" __divdi3 "$runtime_div_locals" \
	>/dev/null 2>&1; then
	printf '%s\n' 'ERROR: second runtime ESP decrement survived' >&2
	exit 1
fi

runtime_push_mutant="$temporary/runtime-extra-push.dis"
cp "$temporary/__divdi3.dis" "$runtime_push_mutant"
printf '%s\n' '  0: 6a 00 push $0x0' >> "$runtime_push_mutant"
if runtime_disassembly_frame "$runtime_push_mutant" __divdi3 "$runtime_div_locals" \
	>/dev/null 2>&1; then
	printf '%s\n' 'ERROR: non-register runtime push survived' >&2
	exit 1
fi

runtime_pushf_mutant="$temporary/runtime-pushf.dis"
cp "$temporary/__divdi3.dis" "$runtime_pushf_mutant"
printf '%s\n' '  0: 9c pushf' >> "$runtime_pushf_mutant"
if runtime_disassembly_frame "$runtime_pushf_mutant" __divdi3 "$runtime_div_locals" \
	>/dev/null 2>&1; then
	printf '%s\n' 'ERROR: operand-less runtime push survived' >&2
	exit 1
fi

runtime_negative_add_mutant="$temporary/runtime-negative-add.dis"
cp "$temporary/__divdi3.dis" "$runtime_negative_add_mutant"
printf '%s\n' '  0: 83 c4 fc add $-4,%esp' >> "$runtime_negative_add_mutant"
if runtime_disassembly_frame "$runtime_negative_add_mutant" __divdi3 \
	"$runtime_div_locals" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: negative runtime ESP addition survived' >&2
	exit 1
fi

for mutation in 'ff e0 jmp *%eax' '75 00 jne 0 <outside_helper>' \
	'e2 00 loop 0 <outside_helper>'; do
	runtime_branch_mutant="$temporary/runtime-branch-mutant.dis"
	cp "$temporary/__divdi3.dis" "$runtime_branch_mutant"
	printf '  0: %s\n' "$mutation" >> "$runtime_branch_mutant"
	if runtime_disassembly_frame "$runtime_branch_mutant" __divdi3 \
		"$runtime_div_locals" >/dev/null 2>&1; then
		printf '%s\n' 'ERROR: outbound runtime branch survived' >&2
		exit 1
	fi
done

# Keep the indirect-target manifest tied to the selected production
# initializers. A field/target change must update both source and graph review.
while IFS='|' read -r source initializer; do
	test -z "$source" && continue
	test "$(grep -Fc "$initializer" "$root/$source")" -eq 1
done <<'EOF'
src/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.c|binding->ops.dma_snapshot = executor_dma_snapshot;
src/mainboard/starlabs/starbook/variants/mtl/mor_clear_x86.c|binding->ops.inventory_validate = executor_inventory_validate;
src/lib/payload_mm_authvar_mor_clear_x86.c|ops->map_window = map_window;
src/lib/payload_mm_authvar_mor_clear_x86.c|ops->cache_writeback_invalidate = cache_writeback_invalidate;
src/lib/payload_mm_authvar_mor_clear_x86.c|ops->fence = fence;
src/lib/payload_mm_authvar_mor_clear_x86.c|ops->unmap_window = unmap_window;
src/lib/payload_mm_authvar_mor_clear_x86.c|.page_tables_init = production_page_tables_init,
src/lib/payload_mm_authvar_mor_clear_x86.c|.map_2m = production_map_2m,
src/lib/payload_mm_authvar_mor_clear_x86.c|.paging_disable = paging_disable_pae,
src/lib/payload_mm_authvar_mor_clear_x86.c|.paging_active = production_paging_active,
src/lib/payload_mm_authvar_mor_clear_x86.c|.clflush_available = clflush_supported,
src/lib/payload_mm_authvar_mor_clear_x86.c|.clflush_range = clflush_region,
src/lib/payload_mm_authvar_mor_clear_x86.c|.memory_fence = production_memory_fence,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|.ensure = platform_ensure,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|.observe = platform_observe,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|.random64 = platform_random64,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|.poison = platform_poison,
src/lib/payload_mm_authvar_mor_live_inventory.c|bootmem_walk_dram(collect_range, &context)
src/soc/intel/common/block/fast_spi/mmap_boot.c|xlate_region_device_ro_init(&real_dev,
src/soc/intel/common/block/fast_spi/mmap_boot.c|mem_region_device_ro_init(&shadow_devs[type],
src/console/printk.c|vtxprintf(wrap_putchar, fmt, args, state.as_ptr);
src/console/printk.c|vtxprintf(console_interactive_tx_byte, fmt, args, NULL);
src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c|.classify_guard = classify_guard,
src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c|.reservations_register = reservations_register,
src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c|.resolve_binding = resolve_binding,
src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c|.private_complete = private_complete,
src/mainboard/starlabs/starbook/variants/mtl/mor_platform.c|.private_close = private_close,
EOF

while IFS='|' read -r source expected initializer; do
	actual=$(grep -Fc "$initializer" "$root/$source")
	if [ "$actual" -ne "$expected" ]; then
		printf 'ERROR: %s has %u instances of %s, expected %u\n' \
			"$source" "$actual" "$initializer" "$expected" >&2
		exit 1
	fi
done <<'EOF'
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|4|.read32 = pci_read32,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|4|.write16 = pci_write16,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|4|.read32 = engine_read32,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|3|.write32 = engine_write32,
src/mainboard/starlabs/starbook/variants/mtl/dma_live_platform.c|3|.commit_tables = commit_tables,
src/commonlib/region.c|2|.mmap = xlate_mmap,
src/commonlib/region.c|2|.munmap = xlate_munmap,
src/commonlib/region.c|2|.mmap = mdev_mmap,
src/commonlib/region.c|2|.munmap = mdev_munmap,
EOF

awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=0 -f "$graph" "$selected_ci"

# The selected production image has no private provider. Verify that it keeps
# only the weak fail-closed platform hook rather than claiming a success proof.
if grep -qx 'CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER=y' \
	"$config/.config"; then
	printf '%s\n' 'ERROR: private provider unexpectedly selected' >&2
	exit 1
fi
"$cross_nm" "$build/cbfs/fallback/ramstage.debug" | awk '
	$3 == "platform_payload_mm_authvar_mor_linear_ops" {
		found++
		if ($2 != "W") bad = 1
	}
	$3 == "starbook_mtl_mor_private_boundary" { bad = 1 }
	END { exit found != 1 || bad }
'

verify_weak_provider()
{
	file=$1
	awk '
		/^[[:space:]]*[0-9a-f]+:/ {
			instruction[++count] = $0
			if ($0 ~ /xor[[:space:]]+%eax,%eax/) {
				zero_count++
				zero_index = count
				split($1, part, ":")
				zero_address = part[1]
			}
			if ($0 ~ /[[:space:]]ret[[:space:]]*$/) ret_count++
			if ($0 ~ /[[:space:]]call[[:space:]]/ && $0 !~ /<memset>/) bad = 1
			if ($0 ~ /[[:space:]]jmp[[:space:]]/) bad = 1
			if ($0 ~ /[[:space:]]j[a-z]+[[:space:]]/ && $0 !~ /[[:space:]]je[[:space:]]/)
				bad = 1
			if ($0 ~ /[[:space:]]je[[:space:]]/) {
				branch_count++
				branch_target = $(NF - 1)
			}
		}
		END {
			if (zero_count != 1 || ret_count != 1 || branch_count != 1 || bad ||
			    branch_target != zero_address ||
			    instruction[count] !~ /[[:space:]]ret[[:space:]]*$/ ||
			    instruction[count - 1] !~ /add[[:space:]]+\$0xc,%esp/ ||
			    zero_index != count - 2)
				exit 1
		}
	' "$file"
}

weak_disassembly="$temporary/weak-provider.dis"
"$cross_objdump" -d --disassemble=platform_payload_mm_authvar_mor_linear_ops \
	"$build/cbfs/fallback/ramstage.debug" > "$weak_disassembly"
verify_weak_provider "$weak_disassembly"
weak_mutant="$temporary/weak-provider-true.dis"
sed 's/xor[[:space:]]*%eax,%eax/mov    $0x1,%eax/' "$weak_disassembly" > "$weak_mutant"
if verify_weak_provider "$weak_mutant"; then
	printf '%s\n' 'ERROR: weak-provider semantic drift survived' >&2
	exit 1
fi
printf '%s\n' 'selected StarBook MTL MOR integration: fail-closed (no private provider)'

# Compile dormant provider glue with the selected compiler/configuration. This
# proves only its 1024-byte callback contract, not a production success path.
make -C "$root" obj="$build" KBUILD_KCONFIG="$config/Kconfig" \
	DOTCONFIG="$config/.config" STACK_AUDIT_CFLAGS="$stack_flags" \
	CONFIG_STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER=y \
	"$build/ramstage/mainboard/starlabs/starbook/variants/mtl/mor_platform.o" \
	>"$temporary/provider-build.log" 2>&1 || {
	cat "$temporary/provider-build.log" >&2
	exit 1
}
cp "$selected_ci" "$contract_ci"
cat "$build/ramstage/mainboard/starlabs/starbook/variants/mtl/mor_platform.ci" \
	>> "$contract_ci"
awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$contract_ci"

reject_omitted_binding()
{
	caller=$1
	if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
		-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
		-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
		-v provider_contract=1 -v omit_caller="$caller" \
		-f "$graph" "$contract_ci" >/dev/null 2>&1; then
		printf 'ERROR: omitted %s callback binding survived\n' "$caller" >&2
		exit 1
	fi
}

for caller in call_inventory payload_mm_authvar_mor_clear_execute \
	vtd_transition_probe pci_bme_quiesce bootmem_walk_dram ensure_seed; do
	reject_omitted_binding "$caller"
done

for target in production_map_2m engine_write32 pci_write16 @private_callback \
	@selected_boot_region_mmap_callback @selected_boot_region_munmap_callback; do
	if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
		-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
		-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
		-v provider_contract=1 -v omit_target="$target" \
		-f "$graph" "$contract_ci" >/dev/null 2>&1; then
		printf 'ERROR: omitted %s target edge survived\n' "$target" >&2
		exit 1
	fi
done

# A new indirect call in a known caller must fail the per-caller count contract.
extra_indirect="$temporary/extra-indirect.ci"
cp "$contract_ci" "$extra_indirect"
printf '%s\n' 'edge: { sourcename: "payload_mm_authvar_mor_clear_execute" targetname: "__indirect_call" }' \
	>> "$extra_indirect"
if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$extra_indirect" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: added indirect call survived' >&2
	exit 1
fi

substituted_site="$temporary/substituted-site.ci"
sed '0,/payload_mm_authvar_mor_clear_executor.c:280:29/s//payload_mm_authvar_mor_clear_executor.c:280:30/' \
	"$contract_ci" > "$substituted_site"
if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$substituted_site" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: same-count indirect call-site substitution survived' >&2
	exit 1
fi

if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -v mutate_site_caller=payload_mm_authvar_mor_clear_execute \
	-v mutate_site_target=executor_inventory_validate -f "$graph" "$contract_ci" \
	>/dev/null 2>&1; then
	printf '%s\n' 'ERROR: indirect target-association substitution survived' >&2
	exit 1
fi

large_frame="$temporary/large-frame.ci"
sed '/title: "starbook_mtl_dma_guard_prepare"/ s/\\n[0-9][0-9]* bytes/\\n5000 bytes/' \
	"$contract_ci" > "$large_frame"
grep -q 'starbook_mtl_dma_guard_prepare.*\\n5000 bytes' "$large_frame"
if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$large_frame" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: oversized reachable frame survived' >&2
	exit 1
fi

large_region_helper="$temporary/large-region-helper.ci"
sed '/label: "xlate_find_window/ s/\\n[0-9][0-9]* bytes/\\n5000 bytes/' \
	"$contract_ci" > "$large_region_helper"
grep -q 'xlate_find_window.*\\n5000 bytes' "$large_region_helper"
if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$large_region_helper" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: oversized region helper frame survived' >&2
	exit 1
fi

unbounded="$temporary/unbounded.ci"
sed '/title: "starbook_mtl_dma_guard_prepare"/ s/(dynamic,bounded)/(dynamic)/' \
	"$contract_ci" > "$unbounded"
grep -q 'starbook_mtl_dma_guard_prepare.*(dynamic)' "$unbounded"
if awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$unbounded" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: dynamic/unbounded reachable frame survived' >&2
	exit 1
fi

printf '%s\n' 'StarBook MTL MOR artifact-derived stack validation: PASS'
