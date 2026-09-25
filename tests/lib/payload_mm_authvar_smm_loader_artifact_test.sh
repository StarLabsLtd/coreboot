#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

crossgcc=${XGCCPATH:-"$root/util/crossgcc/xgcc/bin"}
crossgcc=$(realpath "$crossgcc")
test -x "$crossgcc/i386-elf-gcc"
PATH="$crossgcc:$PATH"
export PATH

prepare_config()
{
	name=$1
	config="$temporary/config-$name"
	mkdir -p "$config"
	cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" \
		"$config/.config"
	if [ "$name" = on ]; then
		cp "$root/src/Kconfig" "$config/Kconfig"
		printf '%s\n' '' 'config TEST_AUTHVAR_SMM_LOADER_ARTIFACT' >> \
			"$config/Kconfig"
		printf '\t%s\n' 'bool' 'default y' \
			'select Q35_PAYLOAD_MM_AUTHVAR_EXECUTOR_TEST_PROOF' \
			'select Q35_PAYLOAD_MM_MOR_TEST_ADAPTER' \
			'select PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER' \
			'select PAYLOAD_MM_AUTHVAR_MOR_POLICY' \
			'select PAYLOAD_MM_AUTHVAR_MOR_COMPLETION_SEAL' \
			'select PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP' \
			'select BOOTMEM_ALIGNED_RESERVATIONS' \
			'select BOOTMEM_ALIGNED_RESERVATION_RECEIPT' \
			'select PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION' \
			'select PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI' >> \
			"$config/Kconfig"
		kconfig="$config/Kconfig"
	else
		kconfig="$root/src/Kconfig"
	fi
	make -C "$root" obj="$temporary/build-$name" \
		KBUILD_KCONFIG="$kconfig" DOTCONFIG="$config/.config" \
		olddefconfig >/dev/null
}

build_config()
{
	name=$1
	config="$temporary/config-$name"
	build="$temporary/build-$name"
	if [ "$name" = on ]; then
		kconfig="$config/Kconfig"
	else
		kconfig="$root/src/Kconfig"
	fi
	stack_flags='-fstack-usage -save-temps=obj'
	make -C "$root" -j4 obj="$build" KBUILD_KCONFIG="$kconfig" \
		DOTCONFIG="$config/.config" STACK_AUDIT_CFLAGS="$stack_flags" \
		"$build/cbfs/fallback/ramstage.debug" \
		>"$temporary/build-$name.log" 2>&1 || {
		cat "$temporary/build-$name.log" >&2
		exit 1
	}
}

prepare_config off
prepare_config on

! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP=y$' \
	"$temporary/config-off/.config"
! grep -q '^CONFIG_PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI=y$' \
	"$temporary/config-off/.config"
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP=y' \
	"$temporary/config-on/.config"
grep -qx 'CONFIG_PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI=y' \
	"$temporary/config-on/.config"
grep -qx '# CONFIG_LTO is not set' "$temporary/config-on/.config"

build_config off
build_config on

xcompile="$temporary/build-on/xcompile"
cross_cc=$(awk -F ':=' '$1 == "GCC_CC_x86_32" {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
	print $2
	exit
}' "$xcompile")
cross_nm=$(awk -F ':=' '$1 ~ /^[[:space:]]*NM_x86_32$/ {
	gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
	print $2
	exit
}' "$xcompile")
cross_cc=$(command -v "$cross_cc")
cross_nm=$(command -v "$cross_nm")
for tool in "$cross_cc" "$cross_nm"; do
	case $(realpath "$tool") in
	"$crossgcc"/*) ;;
	*)
		printf 'ERROR: build selected unpinned tool: %s\n' "$tool" >&2
		exit 1
		;;
	esac
done

off_ramstage="$temporary/build-off/cbfs/fallback/ramstage.debug"
on_ramstage="$temporary/build-on/cbfs/fallback/ramstage.debug"
on_smm="$temporary/build-on/ramstage/cpu/x86/smm/smm.manual"
off_symbols="$temporary/off.symbols"
on_symbols="$temporary/on.symbols"
smm_symbols="$temporary/smm.symbols"
"$cross_nm" "$off_ramstage" > "$off_symbols"
"$cross_nm" "$on_ramstage" > "$on_symbols"
"$cross_nm" "$on_smm" > "$smm_symbols"

symbol_present()
{
	symbol=$1
	file=$2
	awk -v symbol="$symbol" '$3 == symbol { found++ }
		END { exit found != 1 }' "$file"
}

symbol_absent()
{
	symbol=$1
	file=$2
	! awk -v symbol="$symbol" '$3 == symbol { found = 1 }
		END { exit !found }' "$file"
}

symbol_present smm_load_module "$off_symbols"
symbol_present smm_load_module "$on_symbols"
for symbol in payload_mm_authvar_smm_arena_reserve \
	payload_mm_authvar_mor_private_smi_loader_provision \
	platform_payload_mm_authvar_smm_arena_required \
	platform_payload_mm_authvar_smm_arena_abort; do
	symbol_absent "$symbol" "$off_symbols"
	symbol_present "$symbol" "$on_symbols"
	symbol_absent "$symbol" "$smm_symbols"
done

stack_file=$(find "$temporary/build-on/ramstage" -type f \
	-name 'smm_module_loader.su' -print)
test "$(printf '%s\n' "$stack_file" | sed '/^$/d' | wc -l)" -eq 1
stack_bytes=$(awk '$1 ~ /:smm_load_module$/ { print $2; found++ }
	END { if (found != 1) exit 1 }' "$stack_file")
case $stack_bytes in
''|*[!0-9]*) exit 1 ;;
esac
stack_size=$(awk '$1 == "#define" && $2 == "CONFIG_STACK_SIZE" {
	print $3
	found++
}
	END { if (found != 1) exit 1 }' "$temporary/build-on/config.h")
stack_limit=$((stack_size / 4))
test "$stack_limit" -gt 0

stack_within_limit()
{
	actual=$1
	limit=$2
	test "$actual" -le "$limit"
}

stack_within_limit "$stack_bytes" "$stack_limit"
if stack_within_limit $((stack_limit + 1)) "$stack_limit"; then
	printf '%s\n' 'ERROR: oversized loader stack mutation survived' >&2
	exit 1
fi

printf 'selected coreboot toolchain: %s\n' "$crossgcc"
printf 'smm_load_module stack: %u / %u bytes (%u-byte ramstage stack)\n' \
	"$stack_bytes" "$stack_limit" "$stack_size"
printf '%s\n' 'Payload-MM authvar SMM loader artifact tests: PASS'
