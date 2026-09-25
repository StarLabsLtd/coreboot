#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=${FMP_TEST_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)}
root=$(realpath "$root")
temporary=$(mktemp -d)
cleanup()
{
	if [ "${KEEP_MTL_CAPSULE_TMP:-0}" = 1 ]; then
		printf 'preserved capsule artifacts: %s\n' "$temporary" >&2
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

resolve_config()
{
	name=$1
	kconfig=$2
	input=$3
	build_name=${4:-$name}

	mkdir -p "$temporary/build-$build_name"
	cp "$input" "$temporary/config-$name"
	make -C "$root" -s obj="$temporary/build-$build_name" \
		KBUILD_KCONFIG="$kconfig" DOTCONFIG="$temporary/config-$name" \
		olddefconfig
}

assert_fmp_sha_dependency()
{
	kconfig=$1
	awk '
		$1 == "config" {
			inside = ($2 == "PAYLOAD_MM_FMP_OWNER_AUTHVAR")
			next
		}
		inside && $1 == "depends" && $2 == "on" &&
			$3 == "PAYLOAD_MM_CMS_CORE" { found++ }
		END { exit found != 1 }
	' "$kconfig"
}

# The FMP transaction hashes its canonical record independently of the wider
# Auth2 verifier closure. Keep that provider dependency direct and hostile to
# future changes in the coordinator's transitive selection graph.
assert_fmp_sha_dependency "$root/src/lib/Kconfig"
missing_sha_kconfig="$temporary/Kconfig-missing-fmp-sha"
awk '
	$1 == "config" {
		inside = ($2 == "PAYLOAD_MM_FMP_OWNER_AUTHVAR")
	}
	inside && $1 == "depends" && $2 == "on" &&
		$3 == "PAYLOAD_MM_CMS_CORE" { next }
	{ print }
' "$root/src/lib/Kconfig" > "$missing_sha_kconfig"
if assert_fmp_sha_dependency "$missing_sha_kconfig"; then
	printf '%s\n' 'ERROR: missing direct FMP SHA dependency survived' >&2
	exit 1
fi

# Explicitly disabling the hidden prerequisite must resolve to the exact same
# production selection as leaving it absent from the release configuration.
resolve_config default "$root/src/Kconfig" \
	"$root/configs/config.starlabs_starbook_mtl" release
cp "$root/configs/config.starlabs_starbook_mtl" "$temporary/release-off"
printf '%s\n' \
	'# CONFIG_STARLABS_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES is not set' \
	>> "$temporary/release-off"
resolve_config explicit-off "$root/src/Kconfig" "$temporary/release-off" release
cmp "$temporary/config-default" "$temporary/config-explicit-off"

grep -qx 'CONFIG_DRIVERS_EFI_UPDATE_CAPSULES=y' "$temporary/config-default"
grep -qx 'CONFIG_DRIVERS_EFI_GENERATE_CAPSULE=y' "$temporary/config-default"
grep -qx 'CONFIG_SMMSTORE=y' "$temporary/config-default"
for symbol in CAPSULE_BROKER_CONTRACT CAPSULE_PLATFORM_FACTS \
	CAPSULE_PLATFORM_ADAPTERS PAYLOAD_MM_FMP_OWNER_AUTHVAR; do
	if grep -q "^CONFIG_${symbol}=y$" "$temporary/config-default"; then
		printf 'ERROR: default config selected %s\n' "$symbol" >&2
		exit 1
	fi
done

sed -n '1,3p' "$root/src/Kconfig" > "$temporary/Kconfig-selected"
printf '%s\n' 'config SMM_MODULE_STACK_SIZE' >> "$temporary/Kconfig-selected"
printf '\t%s\n' 'hex' 'default 0x4000' >> "$temporary/Kconfig-selected"
sed -n '5,$p' "$root/src/Kconfig" >> "$temporary/Kconfig-selected"
cat >> "$temporary/Kconfig-selected" <<'EOF'

config TEST_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES
	def_bool y
	select BOOTMEDIA_SMM_BWP
	select SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW
	select STARLABS_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES
	select PAYLOAD_MM_AUTHVAR_STORE_SCANNER
	select PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS
	select PAYLOAD_MM_AUTHVAR_FTW_DECODER
	select PAYLOAD_MM_AUTHVAR_MEDIA_PORT
	select PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND
	select PAYLOAD_MM_AUTHVAR_WRITER
	select PAYLOAD_MM_AUTHVAR_EXECUTOR
	select PAYLOAD_MM_AUTHVAR_FORMAT_PARSER
	select PAYLOAD_MM_AUTHVAR_SIGNATURE_DB
	select PAYLOAD_MM_AUTHVAR_CERTDB
	select PAYLOAD_MM_AUTHVAR_ROUTE
	select PAYLOAD_MM_AUTHVAR_CMS_VERIFY
	select PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING
	select PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST
	select PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR
	select PAYLOAD_MM_AUTHVAR_TRUST_STORE
	select PAYLOAD_MM_AUTHVAR_AUTHORITY
	select PAYLOAD_MM_AUTHVAR_BUNDLE_PLAN
	select PAYLOAD_MM_AUTHVAR_CANDIDATE
	select PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT
	select PAYLOAD_MM_AUTHVAR_COORDINATOR
	select PAYLOAD_MM_FMP_OWNER_AUTHVAR
EOF
sed -e '/^CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION=/d' \
	-e '/^CONFIG_SMM_MODULE_STACK_SIZE=/d' \
	"$root/configs/config.starlabs_starbook_mtl" > "$temporary/config-selected-input"
printf '%s\n' '# CONFIG_BOOTMEDIA_SMM_BWP_RUNTIME_OPTION is not set' \
	'CONFIG_SMM_MODULE_STACK_SIZE=0x4000' >> \
	"$temporary/config-selected-input"
resolve_config selected "$temporary/Kconfig-selected" \
	"$temporary/config-selected-input"

for symbol in \
	STARLABS_STARBOOK_MTL_CAPSULE_PLATFORM_PREREQUISITES \
	BOOTMEDIA_SMM_BWP \
	SOC_INTEL_COMMON_BLOCK_SMM_SPI_WINDOW \
	CAPSULE_UPDATE_CONTRACT \
	PAYLOAD_MM_AUTHVAR_CONTRACT \
	CAPSULE_BROKER_CONTRACT \
	CAPSULE_BROKER_FIXED_BUFFERS \
	CAPSULE_BROKER_CBMEM_BUFFERS \
	CAPSULE_PLATFORM_FACTS \
	CAPSULE_PLATFORM_ADAPTERS \
	PAYLOAD_MM_AUTHVAR_STORE_SCANNER \
	PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS \
	PAYLOAD_MM_AUTHVAR_FTW_DECODER \
	PAYLOAD_MM_AUTHVAR_MEDIA_PORT \
	PAYLOAD_MM_AUTHVAR_SMMSTORE_BACKEND \
	PAYLOAD_MM_AUTHVAR_WRITER \
	PAYLOAD_MM_AUTHVAR_EXECUTOR \
	PAYLOAD_MM_AUTHVAR_COORDINATOR \
	PAYLOAD_MM_FMP_OWNER_AUTHVAR \
	PAYLOAD_MM_CMS_CORE \
	SPI_FLASH_SMM; do
	grep -qx "CONFIG_${symbol}=y" "$temporary/config-selected"
done

# Typed and legacy update paths must remain mutually exclusive.
for symbol in DRIVERS_EFI_UPDATE_CAPSULES SMMSTORE_FULL_FLASH_ACCESS \
	CAPSULE_BROKER_ENDPOINT_PUBLICATION; do
	if grep -q "^CONFIG_${symbol}=y$" "$temporary/config-selected"; then
		printf 'ERROR: selected fixture enabled forbidden %s\n' "$symbol" >&2
		exit 1
	fi
done

selected_build="$temporary/build-selected"
stack_flags='-fstack-usage -fcallgraph-info=su -fdump-ipa-cgraph -save-temps=obj'
fmp_roots='-u payload_mm_authvar_fmp_state_transaction -u payload_mm_authvar_fmp_state_initialize -u payload_mm_fmp_owner_authvar_reservation -u payload_mm_fmp_owner_authvar_identity_install'
wrapper_root='--wrap=mbedtls_rsa_parse_pubkey -u __wrap_mbedtls_rsa_parse_pubkey'
make -C "$root" -j4 obj="$selected_build" \
	KBUILD_KCONFIG="$temporary/Kconfig-selected" \
	DOTCONFIG="$temporary/config-selected" \
	STACK_AUDIT_CFLAGS="$stack_flags" \
	"$selected_build/smm/smm.elf-ldflags=$wrapper_root $fmp_roots" \
	"$selected_build/cbfs/fallback/ramstage.debug" \
	"$selected_build/ramstage/cpu/x86/smm/smm.manual" \
	> "$temporary/build-selected.log" 2>&1 || {
	cat "$temporary/build-selected.log" >&2
	exit 1
}

cross_nm=$(awk -F ':=' '$1 ~ /^[[:space:]]*NM_x86_32$/ {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
	print $2
	exit
}' "$selected_build/xcompile")
cross_nm=$(command -v "$cross_nm")
cross_objdump=$(awk -F ':=' '$1 ~ /^[[:space:]]*OBJDUMP_x86_32$/ {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
	print $2
	exit
}' "$selected_build/xcompile")
cross_objdump=$(command -v "$cross_objdump")
case $(realpath "$cross_nm") in
"$crossgcc"/*) ;;
*)
	printf 'ERROR: selected build used unpinned nm: %s\n' "$cross_nm" >&2
	exit 1
	;;
esac
case $(realpath "$cross_objdump") in
"$crossgcc"/*) ;;
*)
	printf 'ERROR: selected build used unpinned objdump: %s\n' "$cross_objdump" >&2
	exit 1
	;;
esac

archive_symbols="$temporary/smm-archive.symbols"
final_symbols="$temporary/smm-final.symbols"
manual_symbols="$temporary/smm-manual.symbols"
default_final_symbols="$temporary/smm-default-final.symbols"
ramstage_symbols="$temporary/selected-ramstage-final.symbols"

# The production/default-off artifact is a separate final link, not merely a
# Kconfig claim. Its final symbol view must contain no dormant FMP owner or
# executor entry point.
release_build="$temporary/build-release"
make -C "$root" -j4 obj="$release_build" \
	KBUILD_KCONFIG="$root/src/Kconfig" \
	DOTCONFIG="$temporary/config-default" \
	"$release_build/smm/smm.elf" > "$temporary/build-default.log" 2>&1 || {
	cat "$temporary/build-default.log" >&2
	exit 1
}
"$cross_nm" -g --defined-only "$release_build/smm/smm.elf" > \
	"$default_final_symbols"
for symbol in payload_mm_authvar_fmp_state_transaction \
	payload_mm_authvar_fmp_state_initialize \
	payload_mm_fmp_owner_authvar_reservation \
	payload_mm_fmp_owner_authvar_identity_install; do
	if awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$default_final_symbols"; then
		printf 'ERROR: default-off final SMM contains forbidden symbol: %s\n' \
			"$symbol" >&2
		exit 1
	fi
done

"$cross_nm" -g --defined-only "$selected_build/smm/smm.a" > \
	"$archive_symbols"
"$cross_nm" -g --defined-only \
	"$selected_build/smm/smm.elf" > "$final_symbols"
"$cross_nm" -g --defined-only \
	"$selected_build/ramstage/cpu/x86/smm/smm.manual" > "$manual_symbols"
"$cross_nm" -g --defined-only \
	"$selected_build/cbfs/fallback/ramstage.debug" > "$ramstage_symbols"
for symbol in payload_mm_authvar_fmp_state_transaction \
	payload_mm_authvar_fmp_state_initialize \
	payload_mm_fmp_owner_authvar_reservation \
	payload_mm_fmp_owner_authvar_identity_install; do
	if awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$ramstage_symbols"; then
		printf 'ERROR: selected ramstage contains forbidden SMM symbol: %s\n' \
			"$symbol" >&2
		exit 1
	fi
done
# Selection must place the concrete implementations in the MTL SMM link input,
# but the final module must discard them until a reviewed composition calls
# them. Merely selecting prerequisites must not install a runtime service.
for symbol in \
	capsule_platform_facts_collect \
	capsule_platform_media_adapter_build \
	capsule_platform_smm_storage_contains \
	capsule_platform_identity_build; do
	awk -v symbol="$symbol" '$3 == symbol { found++ }
		END { exit found != 1 }' "$archive_symbols"
	if awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$manual_symbols"; then
		printf 'ERROR: dormant capsule symbol reached smm.manual: %s\n' \
			"$symbol" >&2
		exit 1
	fi
done

# These are deliberately dormant smm.elf measurement roots, not a runtime
# composition in the GC-converted smm.manual. The transaction, initialization,
# and reservation roots share the selected real SMM closure. Identity
# installation remains a separate root until a platform binds a concrete
# protected-storage callback.
for symbol in \
	payload_mm_authvar_fmp_state_transaction \
	payload_mm_authvar_fmp_state_initialize \
	payload_mm_fmp_owner_authvar_reservation \
	payload_mm_fmp_owner_authvar_identity_install \
	payload_mm_sha256 \
	payload_mm_fmp_owner_record_valid \
	payload_mm_fmp_state_transition_valid; do
	awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$final_symbols"
done

# Forced dormant measurement roots belong only to smm.elf. The production-like
# GC link remains free of an uncomposed initialization entry point.
if awk '$3 == "payload_mm_authvar_fmp_state_initialize" { found = 1 }
	END { exit !found }' "$manual_symbols"; then
	printf '%s\n' \
		'ERROR: dormant FMP initialization root reached smm.manual' >&2
	exit 1
fi

# This independent dormant root proves that the selected link retained the
# existing --wrap contract. It is not part of, or reachable from, the FMP
# transaction and initialization roots and must be excluded from their later
# independent rooted stack maxima.
awk '$3 == "__wrap_mbedtls_rsa_parse_pubkey" { found = 1 }
	END { exit !found }' "$final_symbols"

for object in payload_mm_authvar_executor payload_mm_fmp_owner_authvar; do
	test -f "$selected_build/smm/lib/$object.o"
	if test -e "$selected_build/ramstage/lib/$object.o"; then
		printf 'ERROR: SMM-only object reached ramstage: %s\n' "$object" >&2
		exit 1
	fi
	discovered=$(find "$selected_build/smm/lib" -type f \
		\( -name "$object.c.*.ci" -o -name "$object.c.*.su" \
		-o -name "$object.ci" -o -name "$object.su" \) | wc -l)
	test "$discovered" -ge 2
done

if find "$selected_build/smm/lib" -type f -name '*stub*.o' | grep -q .; then
	printf '%s\n' 'ERROR: selected authvar SMM closure contains a stub object' >&2
	exit 1
fi

# Bind the dormant stack proof to the selected immutable bus-zero controller
# and its exact volatile-lease operations. The controller deliberately has no
# claim/release callbacks; the lease retains the four sealed flash callbacks.
fast_spi_source="$root/src/soc/intel/common/block/fast_spi/fast_spi_flash.c"
test "$(grep -Fc '.setup = fast_spi_flash_ctrlr_setup,' "$fast_spi_source")" -eq 1
test "$(grep -Fc '.flash_probe = fast_spi_flash_probe,' "$fast_spi_source")" -eq 1
test "$(grep -Fc '.read = fast_spi_flash_read,' "$fast_spi_source")" -eq 1
test "$(grep -Fc '.write = fast_spi_flash_write,' "$fast_spi_source")" -eq 1
test "$(grep -Fc '.erase = fast_spi_flash_erase,' "$fast_spi_source")" -eq 1
test "$(grep -Fc '.status = fast_spi_flash_status,' "$fast_spi_source")" -eq 1
if sed -n '/const struct spi_ctrlr fast_spi_flash_ctrlr = {/,/};/p' \
	"$fast_spi_source" | grep -Eq '\.(claim_bus|release_bus)[[:space:]]='; then
	printf '%s\n' 'ERROR: selected Fast-SPI controller gained claim/release callbacks' >&2
	exit 1
fi
test "$(grep -Fc \
	'.ctrlr = &fast_spi_flash_ctrlr, .bus_start = 0, .bus_end = 0' \
	"$root/src/soc/intel/common/block/spi/spi.c")" -eq 1

# GCC runtime helpers have no .su record. Derive each retained helper's exact
# conservative frame from this selected SMM ELF, and prove __divdi3 is absent.
runtime_frame()
{
	symbol=$1
	disassembly="$temporary/$symbol.dis"
	"$cross_objdump" -d --disassemble="$symbol" "$selected_build/smm/smm.elf" > \
		"$disassembly"
	locals_hex=$(sed -n \
		's/^.*sub[[:space:]]\+\$\(0x[0-9a-f]\+\),%esp.*$/\1/p' \
		"$disassembly")
	test -n "$locals_hex"
	test "$(grep -Ec '^[[:space:]]*[0-9a-f]+:.*push[a-z]*[[:space:]]+%' \
		"$disassembly")" -eq 4
	test "$(grep -Ec 'sub[[:space:]]+.*,%esp' "$disassembly")" -eq 1
	if grep -Eq '[[:space:]](call|enter|pusha|leave|iret)[a-z]*([[:space:]]|$)|[[:space:]](inc|dec|pop|push)[a-z]*[[:space:]]+%esp|[[:space:]]xchg[a-z]*[[:space:]]+.*%esp' \
		"$disassembly"; then
		return 1
	fi
	set -- $("$cross_nm" -S "$selected_build/smm/smm.elf" | \
		awk -v symbol="$symbol" '$4 == symbol { print $1, $2; found++ }
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

if "$cross_nm" -S "$selected_build/smm/smm.elf" | \
	awk '$4 == "__divdi3" { found = 1 } END { exit !found }'; then
	printf '%s\n' 'ERROR: selected SMM unexpectedly contains __divdi3' >&2
	exit 1
fi
runtime_udiv_bytes=$(runtime_frame __udivdi3)
runtime_udivmod_bytes=$(runtime_frame __udivmoddi4)
runtime_umod_bytes=$(runtime_frame __umoddi3)
selected_ci="$temporary/selected-smm.ci"
find "$selected_build/smm" -type f -name '*.ci' -exec cat {} + > "$selected_ci"
stack_size=$(awk -F= '$1 == "CONFIG_SMM_MODULE_STACK_SIZE" {
	print $2; found++
} END { if (found != 1) exit 1 }' "$temporary/config-selected")
stack_size=$((stack_size))
test "$stack_size" -eq 16384
graph_run()
{
	awk -v runtime_div_absent=1 \
		-v runtime_udiv_bytes="$runtime_udiv_bytes" \
		-v runtime_udivmod_bytes="$runtime_udivmod_bytes" \
		-v runtime_umod_bytes="$runtime_umod_bytes" \
		-v caller_reserve=512 -v stack_size="$stack_size" "$@" \
		-f "$root/tests/lib/starbook_mtl_fmp_stack_graph.awk" "$selected_ci"
}

graph_run

killed=0
if graph_run -v omit_root=1 > /dev/null 2>&1; then
	printf '%s\n' 'ERROR: omitted FMP transaction root survived' >&2
	exit 1
fi
killed=$((killed + 1))
while IFS= read -r edge; do
	if graph_run -v omit_edge="$edge" > /dev/null 2>&1; then
		printf 'ERROR: omitted rooted edge survived: %s\n' "$edge" >&2
		exit 1
	fi
	killed=$((killed + 1))
done <<'EOF'
payload_mm_authvar_fmp_state_transaction|media_begin
payload_mm_authvar_fmp_state_transaction|recover_session
payload_mm_authvar_fmp_state_transaction|execute_direct
payload_mm_authvar_fmp_state_transaction|execute_reclaim
payload_mm_authvar_fmp_state_transaction|verify_media
payload_mm_authvar_fmp_state_transaction|payload_mm_authvar_store_scan
payload_mm_authvar_media_begin|begin
begin|intel_smm_spi_window_begin
begin|intel_smm_spi_window_prove
end|intel_smm_spi_window_end
EOF
if graph_run -v omit_initialize_root=1 > /dev/null 2>&1; then
	printf '%s\n' 'ERROR: omitted FMP initialization root survived' >&2
	exit 1
fi
killed=$((killed + 1))
while IFS= read -r edge; do
	if graph_run -v omit_edge="$edge" > /dev/null 2>&1; then
		printf 'ERROR: omitted initialization edge survived: %s\n' "$edge" >&2
		exit 1
	fi
	killed=$((killed + 1))
done <<'EOF'
payload_mm_authvar_fmp_state_initialize|media_begin
payload_mm_authvar_fmp_state_initialize|recover_session
payload_mm_authvar_fmp_state_initialize|fmp_mutate
payload_mm_authvar_fmp_state_initialize|media_end
fmp_mutate|execute_direct
fmp_mutate|execute_reclaim
fmp_mutate|verify_media
fmp_mutate|snapshot_read
fmp_mutate|payload_mm_authvar_store_scan
EOF
if graph_run -v maximum_limit=4367 > /dev/null 2>&1; then
	printf '%s\n' 'ERROR: one-byte-under transaction bound survived' >&2
	exit 1
fi
killed=$((killed + 1))
if graph_run -v total_limit=4879 > /dev/null 2>&1; then
	printf '%s\n' 'ERROR: one-byte-under total bound survived' >&2
	exit 1
fi
killed=$((killed + 1))
if graph_run -v initialize_maximum_limit=4511 > /dev/null 2>&1; then
	printf '%s\n' 'ERROR: one-byte-under initialization bound survived' >&2
	exit 1
fi
killed=$((killed + 1))
if graph_run -v initialize_total_limit=5023 > /dev/null 2>&1; then
	printf '%s\n' 'ERROR: one-byte-under initialization total survived' >&2
	exit 1
fi
killed=$((killed + 1))
if graph_run -v combined_total_limit=5023 > /dev/null 2>&1; then
	printf '%s\n' 'ERROR: one-byte-under selected total survived' >&2
	exit 1
fi
killed=$((killed + 1))
test "$killed" -eq 26
printf 'FMP rooted stack mutants killed: %u/26\n' "$killed"

hostile_killed=0
for mutation in \
	mutate_root_frame \
	mutate_initialize_root_frame \
	mutate_control_frame \
	mutate_unknown_indirect \
	mutate_shifted_indirect \
	mutate_duplicate_indirect \
	mutate_deep_direct; do
	if graph_run -v "$mutation=1" > /dev/null 2>&1; then
		printf 'ERROR: hostile stack mutation survived: %s\n' "$mutation" >&2
		exit 1
	fi
	hostile_killed=$((hostile_killed + 1))
done
test "$hostile_killed" -eq 7
printf 'FMP hostile stack mutants killed: %u/7\n' "$hostile_killed"

if [ "${FMP_ARTIFACT_MUTANT_CHILD:-0}" != 1 ]; then
	artifact_killed=0
	for mutation in missing_fmp_root missing_initialize_root default_off_symbol \
		ramstage_symbols missing_cms_root ci_only_symbol \
		ci_only_initialize_symbol manual_initialize_symbol; do
		mutant="$temporary/artifact-$mutation.sh"
		awk -v mutation="$mutation" '
		mutation == "ramstage_symbols" && pending_ramstage_symbols {
			print
			if (index($0, "ramstage.debug\" > \"$ramstage_symbols\"")) {
				print "printf \047%s\\n\047 \04700000000 T payload_mm_authvar_fmp_state_transaction\047 >> \"$ramstage_symbols\""
				print "printf \047%s\\n\047 \04700000000 T payload_mm_authvar_fmp_state_initialize\047 >> \"$ramstage_symbols\""
				print "printf \047%s\\n\047 \04700000000 T payload_mm_fmp_owner_authvar_reservation\047 >> \"$ramstage_symbols\""
				changed++
			}
			pending_ramstage_symbols = 0
			next
		}
		mutation == "ramstage_symbols" &&
		$0 == "\"$cross_nm\" -g --defined-only \\" {
			pending_ramstage_symbols = 1
		}
		mutation == "default_off_symbol" && pending_default_symbols {
			print
			print "printf \047%s\\n\047 \04700000000 T payload_mm_authvar_fmp_state_transaction\047 >> \"$default_final_symbols\""
			print "printf \047%s\\n\047 \04700000000 T payload_mm_authvar_fmp_state_initialize\047 >> \"$default_final_symbols\""
			print "printf \047%s\\n\047 \04700000000 T payload_mm_fmp_owner_authvar_reservation\047 >> \"$default_final_symbols\""
			pending_default_symbols = 0
			changed++
			next
		}
		mutation == "default_off_symbol" &&
		$0 ~ /^"\$cross_nm" -g --defined-only "\$release_build\/smm\/smm.elf"/ {
			pending_default_symbols = 1
		}
		mutation == "missing_fmp_root" &&
		/^fmp_roots=/ && index($0, "-u payload_mm_authvar_fmp_state_transaction") {
			if (gsub(/-u payload_mm_authvar_fmp_state_transaction /, "") != 1)
				exit 2
			changed++
		}
		mutation == "missing_initialize_root" &&
		/^fmp_roots=/ && index($0, "-u payload_mm_authvar_fmp_state_initialize") {
			if (gsub(/-u payload_mm_authvar_fmp_state_initialize /, "") != 1)
				exit 2
			changed++
		}
		mutation == "ramstage_objects" &&
		$0 == "for object in payload_mm_authvar_executor payload_mm_fmp_owner_authvar; do" {
			print "mkdir -p \"$selected_build/ramstage/lib\""
			print ": > \"$selected_build/ramstage/lib/payload_mm_authvar_executor.o\""
			print ": > \"$selected_build/ramstage/lib/payload_mm_fmp_owner_authvar.o\""
			changed++
		}
		mutation == "missing_cms_root" && /^wrapper_root=/ {
			$0 = "wrapper_root=\047\047"
			changed++
		}
		mutation == "ci_only_symbol" &&
		$0 == "# Selection must place the concrete implementations in the MTL SMM link input," {
			print "grep -v \047 payload_mm_authvar_fmp_state_transaction$\047 \"$final_symbols\" > \"$temporary/final-without-fmp\""
			print "mv \"$temporary/final-without-fmp\" \"$final_symbols\""
			changed++
		}
		mutation == "ci_only_initialize_symbol" &&
		$0 == "# Selection must place the concrete implementations in the MTL SMM link input," {
			print "grep -v \047 payload_mm_authvar_fmp_state_initialize$\047 \"$final_symbols\" > \"$temporary/final-without-fmp-init\""
			print "mv \"$temporary/final-without-fmp-init\" \"$final_symbols\""
			changed++
		}
		mutation == "manual_initialize_symbol" &&
		$0 == "# Forced dormant measurement roots belong only to smm.elf. The production-like" {
			print "printf \047%s\\n\047 \04700000000 T payload_mm_authvar_fmp_state_initialize\047 >> \"$manual_symbols\""
			changed++
		}
		{ print }
		END { if (changed != 1) exit 2 }
		' "$0" > "$mutant"
		chmod +x "$mutant"
		mutant_log="$temporary/artifact-$mutation.log"
		if FMP_TEST_ROOT="$root" FMP_ARTIFACT_MUTANT_CHILD=1 \
			XGCCPATH="$crossgcc" "$mutant" > "$mutant_log" 2>&1; then
			printf 'ERROR: selected-artifact mutation survived: %s\n' \
				"$mutation" >&2
			exit 1
		fi
		if grep -Eq ':[[:space:]]*[0-9]+: (=|[^:]*): not found$' \
			"$mutant_log"; then
			printf 'ERROR: selected-artifact mutation used malformed shell: %s\n' \
				"$mutation" >&2
			cat "$mutant_log" >&2
			exit 1
		fi
		artifact_killed=$((artifact_killed + 1))
	done
	test "$artifact_killed" -eq 8
	printf 'FMP selected-artifact mutants killed: %u/8\n' "$artifact_killed"
fi

printf '%s\n' \
	'StarBook MTL dormant capsule platform prerequisites artifact test: PASS'
