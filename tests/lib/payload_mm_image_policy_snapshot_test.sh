#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
mbedtls_source=${MBEDTLS_SOURCE:-$root/3rdparty/mbedtls}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir "$temporary/include"
printf '%s\n' '#include <stdint.h>' '#include <commonlib/helpers.h>' \
	'#define DEVTREE_CONST const' \
	'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;' \
	'typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;' \
	> "$temporary/include/host-types.h"

set -- "$root/tests/lib/payload_mm_image_policy_snapshot_test.c"
for source in bootmem_reservation_receipt payload_mm_authvar_smm_bootstrap \
	payload_mm_authvar payload_mm_authvar_runtime payload_mm_authvar_media \
	payload_mm_authvar_executor payload_mm_authvar_service payload_mm_authvar_service_transaction \
	payload_mm_authvar_coordinator payload_mm_authvar_candidate payload_mm_crypto/crypto \
	payload_mm_crypto/cms payload_mm_crypto/mbedtls_verify_wrap \
	payload_mm_authvar_authority_provider payload_mm_authvar_authority \
	payload_mm_authvar_private_binding payload_mm_authvar_private_trust \
	payload_mm_authvar_trust_store payload_mm_authvar_trust_anchor \
	payload_mm_authvar_signature_db payload_mm_authvar_set_preflight \
	payload_mm_authvar_controlled_mode payload_mm_authvar_view payload_mm_authvar_bundle \
	payload_mm_authvar_certdb payload_mm_authvar_mode payload_mm_authvar_format \
	payload_mm_authvar_route payload_mm_authvar_fv payload_mm_authvar_ftw \
	payload_mm_authvar_store payload_mm_authvar_store_semantics payload_mm_authvar_record \
	payload_mm_authvar_writer; do
	set -- "$@" "$root/src/lib/$source.c"
done
for source in asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time \
	md oid pk pkparse rsa rsa_alt_helpers sha256 sha512 x509 x509_crt platform_util; do
	set -- "$@" "$mbedtls_source/library/$source.c"
done
for block_size in 4096 65536; do
	printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
		'#define CONFIG_MAX_CPUS 4' '#define CONFIG_SMMSTORE 0' \
		'#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS 0' \
		"#define CONFIG_SMMSTORE_BLOCK_SIZE $block_size" \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR 1' \
		'#define CONFIG_PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK 0' \
		'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 0' \
		'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' > "$temporary/include/config.h"
	for optimization in 0 2; do
		${HOSTCC:-cc} -std=gnu11 -O"$optimization" -g -Wall -Wextra -Werror \
			-Wshadow -Wstrict-prototypes -fno-builtin -fno-pie -no-pie \
			-ffunction-sections -fdata-sections -Wl,--gc-sections \
			-fsanitize=address,undefined -fno-sanitize-recover=all \
			-fno-omit-frame-pointer -DBOOTMEM_RECEIPT_TEST -D__TEST__ -D__COREBOOT__ -D__SMM__ \
			-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
			-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-include "$temporary/include/host-types.h" \
			-I"$temporary/include" -I"$root/src" -idirafter "$root/src/include" -I"$root/src/lib" \
			-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" -I"$root/src/lib/payload_mm_crypto" \
			-I"$mbedtls_source/include" -I"$mbedtls_source/library" \
			-Wl,--wrap=mbedtls_rsa_parse_pubkey "$@" -pthread -o "$temporary/test"
		for scenario in setup user capacity runtime wrong-namespace end-error; do
			ASAN_OPTIONS=detect_leaks=0 "$temporary/test" "$scenario"
		done
		if [ "$block_size" -eq 65536 ]; then
			ASAN_OPTIONS=detect_leaks=0 "$temporary/test" oversized
		fi
		result=0
		ASAN_OPTIONS=detect_leaks=0 "$temporary/test" proof-drift || result=$?
		test "$result" -eq 77
	done
done
echo 'Actual coherent producer, size-only refusal and protected end-order gates: PASS'
