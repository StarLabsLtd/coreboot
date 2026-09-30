#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_service_abi_test.c" \
		"$root/src/lib/payload_mm_authvar_service.c" \
		-o "$temporary/$name"
	"$temporary/$name"
}

run_test strict-O0 -O0 -fstrict-aliasing -Wpedantic -Wconversion -Wshadow
run_test strict-O2 -O2 -fstrict-aliasing -Wpedantic -Wconversion -Wshadow
run_test sanitized-O0 -O0 -g -fno-omit-frame-pointer -fstrict-aliasing \
	-Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined \
	-fno-sanitize-recover=all
run_test sanitized-O2 -O2 -g -fno-omit-frame-pointer -fstrict-aliasing \
	-Wpedantic -Wconversion -Wshadow -fsanitize=address,undefined \
	-fno-sanitize-recover=all
run_test coordinator-O0 -O0 -DCONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR=1 \
	-fsanitize=address,undefined -fno-sanitize-recover=all
run_test coordinator-O2 -O2 -DCONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR=1 \
	-fsanitize=address,undefined -fno-sanitize-recover=all

compile_and_kill()
{
	name=$1
	expression=$2
	mutant="$temporary/service-$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_service.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_service.c"; then
		printf 'mutation changed nothing: %s\n' "$name" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$temporary/mutant-$name-O$optimization"
		"${CC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
			-Wpedantic -Wconversion -Wshadow -fno-builtin \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_service_abi_test.c" \
			"$mutant" -o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			printf 'mutation survived: %s O%s\n' "$name" "$optimization" >&2
			exit 1
		fi
	done
}

compile_and_kill get-write-protected \
	'/static bool get_response_valid/,/static bool next_response_valid/ { /case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:/d; }'
compile_and_kill get-security-violation \
	'/static bool get_response_valid/,/static bool next_response_valid/ { s/case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:/case PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION:/; }'
compile_and_kill next-write-protected \
	'/static bool next_response_valid/,/static bool set_response_valid/ { /case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:/d; }'
compile_and_kill query-device-error \
	'/static bool query_response_valid/,/static bool lifecycle_response_valid/ { s/response->status != PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR/true/; }'
compile_and_kill query-write-protected \
	'/static bool query_response_valid/,/static bool lifecycle_response_valid/ { s/response->status != PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED/true/; }'
compile_and_kill lifecycle-device-error \
	'/static bool lifecycle_response_valid/,/payload_mm_authvar_service_response_validate/ { s/response->status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR/false/; }'
compile_and_kill lifecycle-write-protected \
	'/static bool lifecycle_response_valid/,/payload_mm_authvar_service_response_validate/ { s/response->status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED/false/; }'

if grep -Eq '(^|[^A-Za-z0-9_])(smram|store_offset|boot_media|flash_offset|block_id|spi_address)([^A-Za-z0-9_]|$)' \
	"$root/src/include/boot/payload_mm_authvar_service.h"; then
	printf '%s\n' 'public authenticated-variable wire exposes private authority' >&2
	exit 1
fi
check_service_linkage()
{
	awk -v validator='smm-$(CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR) += payload_mm_authvar_service.c' \
		-v transaction='smm-$(CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR) += payload_mm_authvar_service_transaction.c' '
		/payload_mm_authvar_service(_transaction)?\.c/ {
			if ($0 == validator) validators++;
			else if ($0 == transaction) transactions++;
			else exit 1;
		}
		END { if (validators != 1 || transactions != 1) exit 1; }
	' "$1"
}

# Only the existing coordinator owner may link protected execution into SMM.
# Linkage is not an installed shared-memory route or endpoint publication proof.
check_service_linkage "$root/src/lib/Makefile.mk"
grep -RlE 'payload_mm_authvar_service(_transaction)?\.c' \
	"$root/src" --include=Makefile.mk > "$temporary/service-linkage-files"
test "$(wc -l < "$temporary/service-linkage-files")" -eq 1
test "$(cat "$temporary/service-linkage-files")" = "$root/src/lib/Makefile.mk"
for prefix in 'smm-y' 'ramstage-$(CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR)'; do
	for source in payload_mm_authvar_service.c payload_mm_authvar_service_transaction.c; do
		awk -v prefix="$prefix" -v source="$source" '
			index($0, " += " source) { $0 = prefix " += " source; }
			{ print; }
		' "$root/src/lib/Makefile.mk" > "$temporary/service-linkage-mutant"
		if check_service_linkage "$temporary/service-linkage-mutant"; then
			printf 'service linkage mutation survived: %s %s\n' "$prefix" "$source" >&2
			exit 1
		fi
	done
done

printf '%s\n' 'Payload-MM authenticated-variable service ABI tests: PASS'
