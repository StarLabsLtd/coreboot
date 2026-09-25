#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
cleanup()
{
	if [ "${KEEP_MTL_MOR_STACK_TMP:-0}" = 1 ]; then
		printf 'preserved stack artifacts: %s\n' "$temporary" >&2
	else
		rm -rf "$temporary"
	fi
}
trap cleanup EXIT HUP INT TERM
config="$temporary/config"
build="$temporary/build"
selected_ci="$temporary/selected.ci"
contract_ci="$temporary/contract.ci"
mkdir -p "$config"

sed -e '/^CONFIG_SMMSTORE=y$/d' \
	-e '/^CONFIG_SMM_MODULE_STACK_SIZE=/d' \
	-e '/^CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION=/d' \
	-e '/^CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y$/d' \
	-e '/^CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y$/d' \
	"$root/configs/config.starlabs_starbook_mtl" > "$config/.config"
printf '%s\n' \
	'# CONFIG_SMMSTORE is not set' \
	'# CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION is not set' \
	'# CONFIG_DRIVERS_EFI_UPDATE_CAPSULES is not set' \
	'# CONFIG_DRIVERS_EFI_GENERATE_CAPSULE is not set' \
	'CONFIG_SMM_MODULE_STACK_SIZE=0x4000' >> "$config/.config"
sed -n '1,3p' "$root/src/Kconfig" > "$config/Kconfig"
printf '%s\n' 'config SMM_MODULE_STACK_SIZE' >> "$config/Kconfig"
printf '\t%s\n' 'hex' 'default 0x4000' >> "$config/Kconfig"
sed -n '5,$p' "$root/src/Kconfig" >> "$config/Kconfig"
printf '%s\n' '' 'config TEST_MTL_MOR_STACK_SELECTOR' >> "$config/Kconfig"
printf '\t%s\n' 'bool' 'default y' \
	'select ENABLE_EARLY_DMA_PROTECTION' \
	'select BOOTMEDIA_SMM_BWP' \
	'select SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW' \
	'select BOOTMEM_ALIGNED_RESERVATIONS' \
	'select BOOTMEM_ALIGNED_RESERVATION_RECEIPT' \
	'select PAYLOAD_MM_AUTHVAR_CONTRACT' \
	'select PAYLOAD_MM_AUTHVAR_STORE_SCANNER' \
	'select PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS' \
	'select PAYLOAD_MM_AUTHVAR_FTW_DECODER' \
	'select PAYLOAD_MM_AUTHVAR_MEDIA_PORT' \
	'select PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND' \
	'select PAYLOAD_MM_AUTHVAR_WRITER' \
	'select PAYLOAD_MM_AUTHVAR_EXECUTOR' \
	'select PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER' \
	'select PAYLOAD_MM_AUTHVAR_FORMAT_PARSER' \
	'select PAYLOAD_MM_AUTHVAR_SIGNATURE_DB' \
	'select PAYLOAD_MM_AUTHVAR_CERTDB' \
	'select PAYLOAD_MM_AUTHVAR_ROUTE' \
	'select PAYLOAD_MM_AUTHVAR_CMS_VERIFY' \
	'select PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING' \
	'select PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST' \
	'select PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR' \
	'select PAYLOAD_MM_AUTHVAR_TRUST_STORE' \
	'select PAYLOAD_MM_AUTHVAR_AUTHORITY' \
	'select PAYLOAD_MM_AUTHVAR_BUNDLE_PLAN' \
	'select PAYLOAD_MM_AUTHVAR_CANDIDATE' \
	'select PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT' \
	'select PAYLOAD_MM_AUTHVAR_COORDINATOR' \
	'select PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER' \
	'select PAYLOAD_MM_AUTHVAR_DEFAULT_STORE' \
	'select PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY' \
	'select PAYLOAD_MM_AUTHVAR_VOLATILE_VIEW' \
	'select PAYLOAD_MM_AUTHVAR_MOR_POLICY' \
	'select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_SEAL' \
	'select PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION' \
	'select STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING' \
	'select PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE' \
	'select PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR' \
	'select PAYLOAD_MM_AUTHVAR_MOR_EARLY_DISCOVERY' \
	'select PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP' \
	'select PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI' \
	'select STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER' >> "$config/Kconfig"
make -C "$root" obj="$build" KBUILD_KCONFIG="$config/Kconfig" \
	DOTCONFIG="$config/.config" olddefconfig >/dev/null
for symbol in BOARD_STARLABS_STARBOOK_MTL \
	STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING \
	STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER \
	PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI \
	PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP \
	PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND \
	SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW \
	PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE \
	PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR; do
	grep -qx "CONFIG_${symbol}=y" "$config/.config"
done
grep -qx '# CONFIG_SMMSTORE is not set' "$config/.config"
grep -qx '# CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION is not set' "$config/.config"
! grep -q '^CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y$' "$config/.config"
! grep -q '^CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y$' "$config/.config"
grep -Eq '^CONFIG_SMM_MODULE_STACK_SIZE=(0x4000|16384)$' "$config/.config"

stack_flags='-fstack-usage -fcallgraph-info=su -fdump-ipa-cgraph -save-temps=obj'
make -C "$root" -j4 obj="$build" KBUILD_KCONFIG="$config/Kconfig" \
	DOTCONFIG="$config/.config" STACK_AUDIT_CFLAGS="$stack_flags" \
	"$build/cbfs/fallback/ramstage.debug" >"$temporary/build.log" 2>&1 || {
	cat "$temporary/build.log" >&2
	exit 1
}
make -C "$root" -j4 obj="$build" KBUILD_KCONFIG="$config/Kconfig" \
	DOTCONFIG="$config/.config" STACK_AUDIT_CFLAGS="$stack_flags" \
	"$build/smm/smm.elf" >>"$temporary/build.log" 2>&1 || {
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
test "$private_callback" -eq 3072
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
	runtime_frame_from_elf "$1" "$build/cbfs/fallback/ramstage.debug" ""
}

runtime_frame_from_elf()
{
	symbol=$1
	elf=$2
	prefix=$3
	disassembly="$temporary/$prefix$symbol.dis"
	"$cross_objdump" -d --disassemble="$symbol" "$elf" > "$disassembly"
	locals_hex=$(sed -n 's/^.*sub[[:space:]]\+\$\(0x[0-9a-f]\+\),%esp.*$/\1/p' \
		"$disassembly")
	test -n "$locals_hex"
	set -- $("$cross_nm" -S "$elf" | awk -v symbol="$symbol" \
		'$4 == symbol { print $1, $2; found++ }
		 END { if (found != 1) exit 1 }')
	start=$((0x$1))
	end=$((start + 0x$2))
	runtime_disassembly_frame "$disassembly" "$symbol" "$locals_hex" \
		"$start" "$end"
}

runtime_disassembly_frame()
{
	disassembly=$1
	symbol=$2
	locals_hex=$3
	start=${4:-}
	end=${5:-}
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
	if [ -n "$start" ]; then
		branch_count=$(grep -Ec '^[[:space:]]*[0-9a-f]+:.*[[:space:]](j[a-z]*|ljmp|loop[a-z]*)([[:space:]]|$)' \
			"$disassembly")
		branch_targets=$(sed -n '/^[[:space:]]*[0-9a-f]\+:.*[[:space:]]\(j[a-z]*\|ljmp\|loop[a-z]*\)[[:space:]]/ {
			s/^.*[[:space:]]\([0-9a-f][0-9a-f]*\)[[:space:]]<.*$/\1/p
		}' "$disassembly")
		test "$(printf '%s\n' "$branch_targets" | sed '/^$/d' | wc -l)" -eq \
			"$branch_count" || return 1
		for target_hex in $branch_targets; do
			target=$((0x$target_hex))
			test "$target" -ge "$start" && test "$target" -lt "$end" || return 1
		done
	else
		! awk -v symbol="$symbol" '/^[[:space:]]*[0-9a-f]+:.*[[:space:]](j[a-z]*|ljmp|loop[a-z]*)([[:space:]]|$)/ {
			if ($0 !~ /</ || index($0, "<" symbol "+") == 0)
				bad = 1
		}
			END { exit !bad }' "$disassembly" || return 1
	fi
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
src/mainboard/starlabs/starbook/variants/mtl/mor_private_boundary.c|.arena_seed = seed,
src/mainboard/starlabs/starbook/variants/mtl/mor_private_boundary.c|.reservations_register = reservations_register,
src/mainboard/starlabs/starbook/variants/mtl/mor_private_boundary.c|.resolve = resolve,
src/mainboard/starlabs/starbook/variants/mtl/mor_private_boundary.c|.complete = complete,
src/mainboard/starlabs/starbook/variants/mtl/mor_private_boundary.c|.close = close,
src/soc/intel/common/block/fast_spi/fast_spi_flash.c|.flash_probe = fast_spi_flash_probe,
src/soc/intel/common/block/fast_spi/fast_spi_flash.c|.setup = fast_spi_flash_ctrlr_setup,
src/soc/intel/common/block/fast_spi/fast_spi_flash.c|.read = fast_spi_flash_read,
src/soc/intel/common/block/fast_spi/fast_spi_flash.c|.write = fast_spi_flash_write,
src/soc/intel/common/block/fast_spi/fast_spi_flash.c|.erase = fast_spi_flash_erase,
src/soc/intel/common/block/fast_spi/fast_spi_flash.c|.status = fast_spi_flash_status,
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

# The Intel controller-specific probe completely initializes the selected
# flash and returns success. That makes spi_flash_probe()'s generic JEDEC
# fallback infeasible for this selected composition. Keep that graph pruning
# tied to the exact source body, and prove the checker rejects semantic drift.
fast_spi_probe_contract()
{
	awk '
		/^static int fast_spi_flash_probe\(/ { in_probe = 1 }
		in_probe && /^static int fast_spi_flash_ctrlr_setup\(/ { in_probe = 0 }
		in_probe && /return[[:space:]]/ {
			returns++
			if ($0 !~ /^[[:space:]]*return 0;[[:space:]]*$/)
				bad = 1
		}
		END { exit returns != 1 || bad }
	' "$1"
}
fast_spi_source="$root/src/soc/intel/common/block/fast_spi/fast_spi_flash.c"
fast_spi_probe_contract "$fast_spi_source"
fast_spi_probe_mutant="$temporary/fast-spi-probe-mutant.c"
sed '/^static int fast_spi_flash_probe(/,/^static int fast_spi_flash_ctrlr_setup(/ {
	/^[[:space:]]*return 0;[[:space:]]*$/ s/return 0;/return -1;/
}' "$fast_spi_source" > "$fast_spi_probe_mutant"
if fast_spi_probe_contract "$fast_spi_probe_mutant"; then
	printf '%s\n' 'ERROR: non-total fast-SPI probe survived source contract' >&2
	exit 1
fi

awk -v limit="$stack_limit" -v private_callback_bytes="$private_callback" \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$selected_ci"

# The selected fixture must contain the strong provider and concrete private
# boundary in the final ramstage artifact. A weak/dormant composition is not a
# production stack proof.
"$cross_nm" "$build/cbfs/fallback/ramstage.debug" | awk '
	$3 == "platform_payload_mm_authvar_mor_linear_ops" {
		provider++
		if ($2 == "W") bad = 1
	}
	$3 == "starbook_mtl_mor_private_boundary" {
		boundary++
		if ($2 == "W") bad = 1
	}
	END { exit provider != 1 || boundary != 1 || bad }
'
cp "$selected_ci" "$contract_ci"
printf '%s\n' 'selected StarBook MTL MOR integration: strong private provider'

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

for target in production_map_2m engine_write32 pci_write16 \
	seed reservations_register resolve complete close \
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

if awk -v limit="$stack_limit" -v private_callback_bytes=2048 \
	-v runtime_div_bytes="$runtime_div_bytes" -v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" -v runtime_umod_bytes="$runtime_umod_bytes" \
	-v provider_contract=1 -f "$graph" "$contract_ci" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: undersized private callback ceiling survived' >&2
	exit 1
fi

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

smm_stack=$(awk '$1 == "#define" && $2 == "CONFIG_SMM_MODULE_STACK_SIZE" {
	print $3; found++
}
	END { if (found != 1) exit 1 }' "$build/config.h")
smm_stack=$((smm_stack))
smm_entry_reserve=512
smm_ci="$temporary/smm.ci"
find "$build/smm" -type f -name '*.ci' -exec cat {} + > "$smm_ci"
test -s "$smm_ci"
smm_elf="$build/smm/smm.elf"
test -s "$smm_elf"
smm_symbols="$temporary/smm.symbols"
"$cross_nm" "$smm_elf" > "$smm_symbols"
symbol_absent()
{
	symbol=$1
	file=$2
	! awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$file"
}

# The selected SMM ELF does not carry __divdi3, so its compiler-graph edge is
# infeasible. Prove exact-symbol absence before granting that exemption, and
# prove the absence checker fails closed when the symbol is present.
symbol_absent __divdi3 "$smm_symbols"
smm_symbols_mutant="$temporary/smm-symbols-with-divdi3"
cp "$smm_symbols" "$smm_symbols_mutant"
printf '%s\n' '00000000 T __divdi3' >> "$smm_symbols_mutant"
if symbol_absent __divdi3 "$smm_symbols_mutant"; then
	printf '%s\n' 'ERROR: present SMM __divdi3 survived absence proof' >&2
	exit 1
fi
smm_runtime_udiv_bytes=$(runtime_frame_from_elf __udivdi3 "$smm_elf" smm-)
smm_runtime_udivmod_bytes=$(runtime_frame_from_elf __udivmoddi4 "$smm_elf" smm-)
smm_runtime_umod_bytes=$(runtime_frame_from_elf __umoddi3 "$smm_elf" smm-)
smm_graph="$root/tests/lib/q35_mor_smm_stack_graph.awk"
smm_limit=$((smm_stack - smm_entry_reserve))
test "$smm_stack" -gt "$smm_entry_reserve"
smm_bound=$(awk -v limit="$smm_limit" -v mtl=1 \
	-v strict_sites=1 -v runtime_div_absent=1 \
	-v runtime_udiv_bytes="$smm_runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$smm_runtime_udivmod_bytes" \
	-v runtime_umod_bytes="$smm_runtime_umod_bytes" \
	-f "$smm_graph" "$smm_ci")
test "$smm_bound" -gt 0
printf 'selected SMM stack budget: %u - %u entry reserve = %u bytes; exact maximum %u\n' \
	"$smm_stack" "$smm_entry_reserve" "$smm_limit" "$smm_bound"

run_smm_graph()
{
	graph_limit=$1
	mutation=${2:-}
	graph_input=${3:-$smm_ci}
	if [ -n "$mutation" ]; then
		awk -v limit="$graph_limit" -v mtl=1 -v strict_sites=1 \
			-v runtime_div_absent=1 \
			-v runtime_udiv_bytes="$smm_runtime_udiv_bytes" \
			-v runtime_udivmod_bytes="$smm_runtime_udivmod_bytes" \
			-v runtime_umod_bytes="$smm_runtime_umod_bytes" \
			-v "$mutation" -f "$smm_graph" "$graph_input"
	else
		awk -v limit="$graph_limit" -v mtl=1 -v strict_sites=1 \
			-v runtime_div_absent=1 \
			-v runtime_udiv_bytes="$smm_runtime_udiv_bytes" \
			-v runtime_udivmod_bytes="$smm_runtime_udivmod_bytes" \
			-v runtime_umod_bytes="$smm_runtime_umod_bytes" \
			-f "$smm_graph" "$graph_input"
	fi
}

# Omitting the platform bootstrap, SMMSTORE installation, or any part of the
# Intel begin/prove/end write window must make the selected graph incomplete.
for mutation in \
	'omit_edge_name=platform_payload_mm_authvar_mor_private_smi_bootstrap' \
	'omit_site=src/lib/payload_mm_authvar_smm_bootstrap.c:466:6' \
	'omit_edge_name=intel_smm_spi_window_begin' \
	'omit_edge_name=intel_smm_spi_window_prove' \
	'omit_edge_name=intel_smm_spi_window_end'; do
	if run_smm_graph "$smm_limit" "$mutation" >/dev/null 2>&1; then
		printf 'ERROR: omitted selected SMM edge survived: %s\n' "$mutation" >&2
		exit 1
	fi
done

# The measured bound itself, rather than a rounded estimate, closes the
# budget. One byte less must fail.
if run_smm_graph $((smm_bound - 1)) "" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: undersized selected SMM stack budget survived' >&2
	exit 1
fi

smm_large_frame="$temporary/smm-large-frame.ci"
sed '/title: "payload_mm_authvar_smm_bootstrap_install"/s/\\n[0-9][0-9]* bytes/\\n16384 bytes/' \
	"$smm_ci" > "$smm_large_frame"
grep -q 'payload_mm_authvar_smm_bootstrap_install.*\\n16384 bytes' "$smm_large_frame"
if run_smm_graph "$smm_limit" "" "$smm_large_frame" >/dev/null 2>&1; then
	printf '%s\n' 'ERROR: oversized selected SMM frame survived' >&2
	exit 1
fi

printf '%s\n' 'StarBook MTL MOR artifact-derived stack validation: PASS'
