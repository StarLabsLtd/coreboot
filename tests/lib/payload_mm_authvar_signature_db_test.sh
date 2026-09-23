#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mbedtls_root="$root/3rdparty/mbedtls"
if [ ! -f "$mbedtls_root/library/x509_crt.c" ]; then
	common_dir=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)
	mbedtls_root=$(CDPATH= cd -- "$(dirname -- "$common_dir")/3rdparty/mbedtls" && pwd)
fi
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-signature-db.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

openssl req -new -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
	-subj /CN=payload-mm-signature-db -keyout "$temporary/key.pem" \
	-out "$temporary/cert.pem" >/dev/null 2>&1
openssl x509 -in "$temporary/cert.pem" -outform DER -out "$temporary/cert.der"

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa rsa_alt_helpers sha256 sha512 x509 x509_crt'
mbedtls_sources=
for source in $sources; do
	mbedtls_sources="$mbedtls_sources $mbedtls_root/library/$source.c"
done

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-DPAYLOAD_MM_AUTH_TEST -DCONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY=1 \
		-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
		-I"$temporary/include" -I"$root/src" -I"$root/src/commonlib/bsd/include" \
		-idirafter "$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/lib" -I"$root/src/arch/x86/include" \
		-I"$root/src/lib/payload_mm_crypto" -I"$mbedtls_root/include" \
		-I"$mbedtls_root/library" \
		"$root/tests/lib/payload_mm_authvar_signature_db_test.c" \
		"$root/src/lib/payload_mm_authvar_signature_db.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_crypto/crypto.c" $mbedtls_sources \
		"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" \
		-Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey -o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output" "$temporary/cert.der"
done
