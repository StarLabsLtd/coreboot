#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
mbedtls_source=${MBEDTLS_SOURCE:-$root/3rdparty/mbedtls}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 4' '#define CONFIG_SMMSTORE 0' \
	'#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS 0' \
	'#define CONFIG_SMMSTORE_BLOCK_SIZE 65536' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK 0' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 0' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' > "$temporary/include/config.h"
for variant in baseline no-request-scrub early-release early-completion; do
	bootstrap_source="$root/src/lib/payload_mm_authvar_smm_bootstrap.c"
	case "$variant" in
	no-request-scrub)
		sed 's/scrub(provider.service_request, sizeof(provider.service_request));/scrub(provider.service_request, 0);/' \
			"$bootstrap_source" > "$temporary/$variant.c"
		;;
	early-release)
		sed '/^enum cb_err payload_mm_authvar_service_execute(void)/,/^}/ {
/__atomic_store_n(&provider.endpoint_phase, ENDPOINT_READY,/d
/scrub(provider.service_request,/i\
\t__atomic_store_n(&provider.endpoint_phase, ENDPOINT_READY, __ATOMIC_RELEASE);
}' "$bootstrap_source" > "$temporary/$variant.c"
		;;
	early-completion)
		sed '/memcpy(mailbox, provider.service_response,/i\
\t__atomic_store_n(&mailbox->completion, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE, __ATOMIC_RELEASE);' \
			"$bootstrap_source" > "$temporary/$variant.c"
		;;
	esac
	if [ "$variant" != baseline ]; then
		cmp -s "$bootstrap_source" "$temporary/$variant.c" && exit 1
		bootstrap_source="$temporary/$variant.c"
	fi
for optimization in 0 2; do
	${CC:-cc} -std=gnu11 -O"$optimization" -g -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fno-builtin -fno-pie -no-pie \
		-ffunction-sections -fdata-sections -Wl,--gc-sections \
		-Wl,--wrap=memcpy \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-omit-frame-pointer -DBOOTMEM_RECEIPT_TEST -D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/lib/payload_mm_crypto" -I"$mbedtls_source/include" \
		"$root/tests/lib/payload_mm_authvar_service_provider_stack_test.c" \
		"$root/src/lib/bootmem_reservation_receipt.c" \
		"$bootstrap_source" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_media.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_service.c" \
		"$root/src/lib/payload_mm_authvar_service_transaction.c" \
		"$root/src/lib/payload_mm_authvar_coordinator.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_crypto/crypto.c" \
		"$root/src/lib/payload_mm_crypto/cms.c" \
		"$mbedtls_source/library/sha256.c" \
		"$mbedtls_source/library/platform_util.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_fv.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" -pthread -o "$temporary/test"
	if [ "$variant" = baseline ]; then
		for mode in normal mailbox-drift; do
			ASAN_OPTIONS=detect_leaks=0 "$temporary/test" "$mode"
		done
		result=0
		ASAN_OPTIONS=detect_leaks=0 "$temporary/test" proof-drift || result=$?
		[ "$result" -eq 77 ]
	else
		result=0
		ASAN_OPTIONS=detect_leaks=0 "$temporary/test" normal \
			> "$temporary/$variant-O$optimization.log" 2>&1 || result=$?
		if [ "$variant" = no-request-scrub ]; then
			[ "$result" -eq 132 ]
		else
			[ "$result" -eq 134 ]
		fi
	fi
done
done
echo 'Real bootstrap and eight-operation provider stack: PASS (host hardware boundaries)'
