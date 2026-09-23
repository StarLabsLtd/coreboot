#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-cms.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

printf 'B\000o\000o\000t\000O\000r\000d\000e\000r\000' > "$temporary/name.bin"
printf '\001\043\105\147\211\253\315\357\020\062\124\166\230\272\334\376' \
	> "$temporary/guid.bin"
printf '\047\000\000\000' > "$temporary/attributes.bin"
printf '\352\007\011\027\014\042\066\000\000\000\000\000\000\000\000\000' \
	> "$temporary/timestamp.bin"
printf 'coreboot-authenticated-variable-payload' > "$temporary/payload.bin"
cat "$temporary/name.bin" "$temporary/guid.bin" "$temporary/attributes.bin" \
	"$temporary/timestamp.bin" "$temporary/payload.bin" > "$temporary/content.bin"
openssl dgst -sha384 -binary "$temporary/content.bin" > "$temporary/content.sha384"

openssl req -new -x509 -newkey rsa:2048 -nodes -days 1 \
	-subj /CN=payload-mm-authvar-cms-test \
	-keyout "$temporary/key.pem" -out "$temporary/cert.pem" >/dev/null 2>&1

for digest in sha256 sha384 sha512; do
	openssl cms -sign -binary -in "$temporary/content.bin" \
		-signer "$temporary/cert.pem" -inkey "$temporary/key.pem" \
		-md "$digest" -outform DER -out "$temporary/$digest.der"
	openssl cms -verify -binary -inform DER -in "$temporary/$digest.der" \
		-content "$temporary/content.bin" -CAfile "$temporary/cert.pem" \
		-purpose any -no_check_time -out /dev/null 2>/dev/null
done

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa sha256 sha512 x509 x509_crt'
mbedtls_sources=
for source in $sources; do
	mbedtls_sources="$mbedtls_sources $root/3rdparty/mbedtls/library/$source.c"
done

run_test()
{
	name=$1
	optimize=$2
	output="$temporary/test-$name"
	${HOSTCC:-cc} -std=c11 "$optimize" -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections -fsanitize=address,undefined \
		'-DCONFIG(option)=CONFIG_##option' \
		-DPAYLOAD_MM_AUTH_TEST \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY=1 \
		-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
		-I"$root/src/commonlib/bsd/include" -idirafter "$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/lib/payload_mm_crypto" \
		-I"$root/3rdparty/mbedtls/include" \
		-I"$root/3rdparty/mbedtls/library" \
		"$root/tests/lib/payload_mm_authvar_cms_test.c" \
		"$root/src/lib/payload_mm_crypto/cms.c" \
		"$root/src/lib/payload_mm_crypto/crypto.c" \
		"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" \
		$mbedtls_sources -Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey \
		-o "$output"
	"$output" "$temporary"
}

run_test O0 -O0
run_test O2 -O2
printf 'OpenSSL Auth2 CMS oracle: PASS (%s)\n' "$(openssl version)"
