#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/mtl-mor-private-stack.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
config="$temporary/config"
build="$temporary/build"
graph="$root/tests/lib/starbook_mtl_mor_private_stack_graph.awk"
combined="$temporary/combined.ci"
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
printf '%s\n' '' 'config TEST_MTL_MOR_PRIVATE_STACK' >> "$config/Kconfig"
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
	'select PAYLOAD_MM_AUTHVAR_MOR_ENTRY_PROBE' \
	'select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_SEAL' \
	'select PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION' \
	'select PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP' \
	'select PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI' \
	'select PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY' \
	'select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RECEIPT' \
	'select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR' \
	'select PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_BACKEND' \
	'select PAYLOAD_MM_AUTHVAR_MOR_LINEAR_ORCHESTRATOR' \
	'select PAYLOAD_MM_AUTHVAR_MOR_EARLY_DISCOVERY' \
	'select STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_BINDING' \
	'select STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER' >> "$config/Kconfig"

make -C "$root" obj="$build" KBUILD_KCONFIG="$config/Kconfig" \
	DOTCONFIG="$config/.config" olddefconfig >/dev/null
for symbol in STARLABS_STARBOOK_MTL_MOR_PLATFORM_PROVIDER \
	PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP \
	PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND PAYLOAD_MM_AUTHVAR_MOR_EARLY_DISCOVERY \
	BOOTMEM_ALIGNED_RESERVATION_RECEIPT BOOTMEDIA_SMM_BWP \
	SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW; do
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
	"$build/cbfs/fallback/ramstage.debug" > "$temporary/build.log" 2>&1 || {
	cat "$temporary/build.log" >&2
	exit 1
}
find "$build/ramstage" -name '*.ci' -exec cat {} + > "$combined"
test -s "$combined"

cross_objdump=$(awk -F ':=' '$1 == "OBJDUMP_x86_32" { print $2; exit }' \
	"$build/xcompile")
cross_objdump=$(command -v "$cross_objdump")
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
	! awk -v locals="$locals_hex" \
		'/^[[:space:]]*[0-9a-f]+:/ && /,%esp([[:space:]]|$)/ {
			if ($0 ~ /sub[[:space:]]+\$/ && index($0, "$" locals ",%esp") != 0)
				next
			if ($0 ~ /add[[:space:]]+\$/ && index($0, "$" locals ",%esp") != 0)
				next
			bad = 1
		}
		END { exit !bad }' "$disassembly" || return 1
	! awk -v symbol="$symbol" \
		'/^[[:space:]]*[0-9a-f]+:.*[[:space:]](j[a-z]*|ljmp|loop[a-z]*)([[:space:]]|$)/ {
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

runtime_div_locals=$(sed -n \
	's/^.*sub[[:space:]]\+\$\(0x[0-9a-f]\+\),%esp.*$/\1/p' \
	"$temporary/__divdi3.dis")
for mutation in \
	'83 ec 04 sub $0x4,%esp' \
	'6a 00 push $0x0' \
	'ff 30 push (%eax)' \
	'9c pushf' \
	'83 c4 fc add $-4,%esp' \
	'ff d0 call *%eax' \
	'c8 04 00 00 enter $0x4,$0x0' \
	'60 pusha' \
	'ff e0 jmp *%eax' \
	'75 00 jne 0 <outside_helper>' \
	'e2 00 loop 0 <outside_helper>'; do
	mutant="$temporary/runtime-mutant.dis"
	cp "$temporary/__divdi3.dis" "$mutant"
	printf '  0: %s\n' "$mutation" >> "$mutant"
	if runtime_disassembly_frame "$mutant" __divdi3 "$runtime_div_locals" \
		>/dev/null 2>&1; then
		printf 'ERROR: runtime mutation survived: %s\n' "$mutation" >&2
		exit 1
	fi
done

result=$(awk -v limit=4096 \
	-v runtime_div_bytes="$runtime_div_bytes" \
	-v runtime_udiv_bytes="$runtime_udiv_bytes" \
	-v runtime_udivmod_bytes="$runtime_udivmod_bytes" \
	-v runtime_umod_bytes="$runtime_umod_bytes" \
	-f "$graph" "$combined")
printf '%s\n' "$result"

for required in send bootmem_aligned_reservation_receipt_emit \
	sign_resolved_reservation bootmem_reservation_receipt_mac sha_update transform; do
	if awk -v limit=4096 -v disconnect_name="$required" \
		-v runtime_div_bytes="$runtime_div_bytes" \
		-v runtime_udiv_bytes="$runtime_udiv_bytes" \
		-v runtime_udivmod_bytes="$runtime_udivmod_bytes" \
		-v runtime_umod_bytes="$runtime_umod_bytes" \
		-f "$graph" "$combined" >/dev/null 2>&1; then
		printf 'ERROR: disconnected rooted target survived: %s\n' \
			"$required" >&2
		exit 1
	fi
done
printf '%s\n' 'StarBook MTL concrete private-boundary stack graph: PASS'
