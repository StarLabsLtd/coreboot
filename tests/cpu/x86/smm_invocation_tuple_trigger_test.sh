#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd -P)
temporary=$(mktemp -d "$root/../.tuple-trigger-test.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
source="$root/src/cpu/x86/smm_invocation_tuple_trigger.c"

mkdir -p "$temporary/include"
for symbol in PAYLOAD_SPI_FLASH_CONSOLE CAPSULE_BROKER_ENDPOINT_PUBLICATION \
	STARLABS_SMM_OPTION_HANDLER STARLABS_ACPI_EFI_OPTION_SMI \
	BOARD_ACER_VN7_572G SMM_APMC_ROUTE_ACPI_CONTROL \
	SMM_APMC_ROUTE_FINALIZE SMM_APMC_ROUTE_AUTHVAR_PRESENCE \
	SMM_APMC_ROUTE_XHCI SMM_APMC_ROUTE_LEGACY \
	DRIVERS_OPTION_CFR_RUNTIME_APPLY TCG_OPAL_S3_UNLOCK \
	SOC_AMD_COMMON_BLOCK_PSP_ROM_ARMOR3 SMM_APMC_ROUTE_SMMINFO \
	SMMSTORE ELOG_GSMI; do
	printf '#define CONFIG_%s 0\n' "$symbol" >> "$temporary/include/config.h"
done

includes="-include $root/src/include/kconfig.h
-include $root/src/include/rules.h
-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
-I$temporary/include -I$root/src/include -I$root/src/commonlib/include
-I$root/src/commonlib/bsd/include -I$root/src/arch/x86/include"
warnings="-Wall -Wextra -Werror -Wshadow -Wundef -Wconversion -Wno-sign-conversion"

source_oracle()
{
	file=$1
	grep -Fq 'uint32_t eax = UINT32_MAX;' "$file" || return 1
	grep -Fq 'uint32_t ecx = UINT32_MAX;' "$file" || return 1
	grep -Fq 'uint16_t dx = APM_CNT;' "$file" || return 1
	grep -Fq '__asm__ __volatile__("outb %%al, %%dx"' "$file" || return 1
	grep -Fq ': "+a" (eax), "+c" (ecx)' "$file" || return 1
	grep -Fq ': "d" (dx)' "$file" || return 1
	grep -Fq ': "memory");' "$file" || return 1
	grep -Fq 'return ((uint64_t)ecx << 32) | eax;' "$file" || return 1
	! grep -Eq 'call_smm|[[:space:]]outb[[:space:]]*\(' "$file" || return 1
}

build()
{
	name=$1
	file=$2
	optimization=$3
	# Deliberate normal flag splitting for this strict freestanding harness.
	# shellcheck disable=SC2086
	${CC:-cc} -m32 -march=i686 -std=gnu11 -O"$optimization" \
		$warnings -ffreestanding -fno-builtin -fno-pie -fstack-usage \
		$includes -c "$file" -o "$temporary/$name.o"
}

object_oracle()
{
	object=$1
	file "$object" | grep -q 'ELF 32-bit' || return 1
	test "$(nm --defined-only "$object" | awk \
		'$3 == "smm_invocation_tuple_trigger" { count++ }
		 END { print count + 0 }')" -eq 1 || return 1
	test -z "$(nm -u "$object")" || return 1
	objdump -d "$object" > "$temporary/disassembly"
	test "$(awk '
		/<smm_invocation_tuple_trigger>:/ { function_body = 1; next }
		function_body && /^[[:xdigit:]]+ <[^>]+>:/ { function_body = 0 }
		function_body && $0 ~ /[[:space:]]call[l]?[[:space:]]/ { count++ }
		END { print count + 0 }' "$temporary/disassembly")" -eq 0 || return 1
	test "$(awk '
		/<smm_invocation_tuple_trigger>:/ { function_body = 1; next }
		function_body && /^[[:xdigit:]]+ <[^>]+>:/ { function_body = 0 }
		function_body && $0 ~ /[[:space:]]outs?[bwl]?[[:space:]]/ { count++ }
		END { print count + 0 }' "$temporary/disassembly")" -eq 1 || return 1
	test "$(awk '
		/<smm_invocation_tuple_trigger>:/ { function_body = 1; next }
		function_body && /^[[:xdigit:]]+ <[^>]+>:/ { function_body = 0 }
		function_body && $2 == "ee" { count++ }
		END { print count + 0 }' "$temporary/disassembly")" -eq 1 || return 1
}

source_oracle "$source"
for optimization in 0 2; do
	build "trigger-O$optimization" "$source" "$optimization"
	object_oracle "$temporary/trigger-O$optimization.o"
	stack=$(awk '$1 ~ /smm_invocation_tuple_trigger$/ { print $2 }' \
		"$temporary/trigger-O$optimization.su")
	case "$stack" in ''|*[!0-9]*) exit 1 ;; esac
	test "$stack" -le 64
done

mutant_must_fail_oracle()
{
	name=$1
	mutant="$temporary/$name.c"
	if cmp -s "$mutant" "$source"; then
		printf 'unchanged tuple-trigger mutant: %s\n' "$name" >&2
		exit 1
	fi
	build "$name" "$mutant" 2
	if source_oracle "$mutant" >/dev/null 2>&1 &&
	   object_oracle "$temporary/$name.o" >/dev/null 2>&1; then
		printf 'surviving tuple-trigger mutant: %s\n' "$name" >&2
		exit 1
	fi
}

sed 's/uint32_t eax = UINT32_MAX;/uint32_t eax = UINT32_MAX - 1U;/' \
	"$source" > "$temporary/low-sentinel.c"
mutant_must_fail_oracle low-sentinel
sed 's/uint32_t ecx = UINT32_MAX;/uint32_t ecx = 0U;/' \
	"$source" > "$temporary/high-sentinel.c"
mutant_must_fail_oracle high-sentinel
sed 's/uint16_t dx = APM_CNT;/uint16_t dx = 0xb3U;/' \
	"$source" > "$temporary/wrong-port.c"
mutant_must_fail_oracle wrong-port
sed 's/outb %%al, %%dx/outl %%eax, %%dx/' \
	"$source" > "$temporary/wide-out.c"
mutant_must_fail_oracle wide-out
sed -e 's/outb %%al, %%dx/outb %%al, $0xb2/' \
	-e 's/: "d" (dx)/:/' -e '/uint16_t dx = APM_CNT;/d' \
	"$source" > "$temporary/immediate-port.c"
mutant_must_fail_oracle immediate-port
sed 's/"+a" (eax)/"=a" (eax)/' "$source" > "$temporary/eax-output-only.c"
mutant_must_fail_oracle eax-output-only
sed 's/"+c" (ecx)/"=c" (ecx)/' "$source" > "$temporary/ecx-output-only.c"
mutant_must_fail_oracle ecx-output-only
sed 's/: "d" (dx)/: "b" (dx)/' "$source" > "$temporary/wrong-port-register.c"
mutant_must_fail_oracle wrong-port-register
sed 's/: "memory");/:);/' "$source" > "$temporary/no-memory-clobber.c"
mutant_must_fail_oracle no-memory-clobber
sed 's/((uint64_t)ecx << 32) | eax/((uint64_t)eax << 32) | ecx/' \
	"$source" > "$temporary/swapped-result.c"
mutant_must_fail_oracle swapped-result
sed 's/return ((uint64_t)ecx << 32) | eax;/return eax;/' \
	"$source" > "$temporary/truncated-result.c"
mutant_must_fail_oracle truncated-result

# Replace only the privileged instruction with a deterministic register shim.
# This executes the production packing expression and proves both input halves,
# the fixed port and the returned high/low ordering.
sed '/__asm__ __volatile__("outb %%al, %%dx"/,/: "memory");/c\
\ttuple_trigger_test_hook(&eax, &ecx, dx);' "$source" > "$temporary/shim-product.c"
sed -e '1i #include <stdint.h>\
void tuple_trigger_test_hook(uint32_t *, uint32_t *, uint16_t);' \
	"$temporary/shim-product.c" > "$temporary/shim-product-declared.c"
# Deliberate normal flag splitting for this host-only transformed shim.
# shellcheck disable=SC2086
${CC:-cc} -std=gnu11 -O2 $warnings $includes \
	"$temporary/shim-product-declared.c" \
	"$root/tests/cpu/x86/smm_invocation_tuple_trigger_shim_test.c" \
	-o "$temporary/shim-test"
"$temporary/shim-test"

grep -Fq '#define SMM_APMC_AUTHVAR_PRESENCE 0xffU' \
	"$root/src/include/cpu/x86/smm_command.h"
grep -Fq '#define SMM_APMC_AUTHVAR_PRESENCE_SENTINEL UINT64_MAX' \
	"$root/src/include/cpu/x86/smm_command.h"
grep -A1 -F '#define PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL' \
	"$root/src/include/boot/payload_mm_authvar_presence_transaction.h" | \
	grep -Fq 'SMM_APMC_AUTHVAR_PRESENCE_SENTINEL'
grep -Fq '#define APM_CNT 0xb2' "$root/src/include/cpu/x86/apm.h"
grep -Fq '#define APMC_OUT_DX_BYTE_IO_MISC (((uint32_t)APM_CNT << 16) | 0x3U)' \
	"$root/src/soc/intel/common/block/smm/invocation_adapter.c"
grep -Fq 'sample.command = inb(APM_CNT);' \
	"$root/src/soc/intel/common/block/smm/invocation_cause.c"

printf '%s\n' 'SMM invocation tuple trigger: PASS'
