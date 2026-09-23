#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
fixtures="$root/tests/data/payload_mm_crypto/fixtures"
output="${TMPDIR:-/tmp}/payload-mm-cms-test.$$"
decoded="$output.fixtures"
trap 'rm -rf "$output" "$decoded"' EXIT HUP INT TERM
mkdir -p "$decoded"

for encoded in "$fixtures"/*.b64; do
	name=$(basename "$encoded" .b64)
	base64 -d "$encoded" > "$decoded/$name"
done
(cd "$decoded" && sha256sum -c "$fixtures/SHA256SUMS" >/dev/null)

# Independent OpenSSL oracle for the generated CDK2/EDK2-compatible profile.
dd if="$decoded/trust.xdr" bs=1 skip=4 status=none | \
	openssl x509 -inform DER -out "$decoded/trust.pem"
dd if="$decoded/other-trust.xdr" bs=1 skip=4 status=none | \
	openssl x509 -inform DER -out "$decoded/other-trust.pem"
{
	cat "$decoded/payload.bin"
	dd if="$decoded/auth.bin" bs=1 count=8 status=none
} > "$decoded/content.bin"
openssl cms -verify -binary -inform DER -in "$decoded/cms.der" \
	-content "$decoded/content.bin" -CAfile "$decoded/trust.pem" \
	-purpose any -no_check_time -partial_chain -out /dev/null 2>/dev/null
if openssl cms -verify -binary -inform DER -in "$decoded/cms.der" \
	-content "$decoded/content.bin" -CAfile "$decoded/other-trust.pem" \
	-purpose any -no_check_time -partial_chain -out /dev/null 2>/dev/null; then
	echo 'OpenSSL accepted the wrong root' >&2
	exit 1
fi
if openssl cms -verify -binary -inform DER -in "$decoded/duplicate-md.der" \
	-content "$decoded/content.bin" -CAfile "$decoded/trust.pem" \
	-purpose any -no_check_time -partial_chain -out /dev/null 2>/dev/null; then
	echo 'OpenSSL accepted duplicate messageDigest' >&2
	exit 1
fi

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa sha256 x509 x509_crt'
objects=
for source in $sources; do
	objects="$objects $root/3rdparty/mbedtls/library/$source.c"
done

${HOSTCC:-cc} -std=c11 "${OPTIMIZE:--O2}" -Wall -Wextra -Werror \
	-ffunction-sections -fdata-sections ${SANITIZE:--fsanitize=address,undefined} \
	'-DCONFIG(option)=CONFIG_##option' \
	-DPAYLOAD_MM_AUTH_TEST \
	-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
	-I"$root/src/commonlib/bsd/include" -idirafter "$root/src/include" \
	-I"$root/src/commonlib/include" \
	-I"$root/src/lib/payload_mm_crypto" \
	-I"$root/3rdparty/mbedtls/include" -I"$root/3rdparty/mbedtls/library" \
	"$root/tests/lib/payload_mm_crypto/cms_test.c" \
	"$root/src/lib/payload_mm_crypto/cms.c" \
	"$root/src/lib/payload_mm_crypto/crypto.c" \
	"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" $objects \
	-Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey -o "$output"

real_auth=
if [ -n "${PAYLOAD_MM_REAL_CAPSULE:-}" ]; then
	test "$(stat -c %s "$PAYLOAD_MM_REAL_CAPSULE")" = 16781014
	test "$(sha256sum "$PAYLOAD_MM_REAL_CAPSULE" | cut -d ' ' -f 1)" = \
		621eb961c265f970ee9e11406a771bf07d3b029b828fa7d37186889da8eccc80
	dd if="$PAYLOAD_MM_REAL_CAPSULE" bs=1 skip=124 count=3618 \
		status=none | cmp - "$decoded/real-cms.der"
	{
		dd if="$PAYLOAD_MM_REAL_CAPSULE" bs=1 skip=3742 status=none
		dd if="$PAYLOAD_MM_REAL_CAPSULE" bs=1 skip=92 count=8 status=none
	} | openssl dgst -sha256 -binary | cmp - "$decoded/real-digest.bin"
	real_auth="$decoded/real-auth.bin"
	dd if="$PAYLOAD_MM_REAL_CAPSULE" of="$real_auth" bs=1 skip=92 status=none
fi

if [ -n "$real_auth" ]; then
	"$output" "$decoded" "$real_auth"
else
	"$output" "$decoded"
fi
printf 'OpenSSL CMS oracle: PASS (%s)\n' "$(openssl version)"
