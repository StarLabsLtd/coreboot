#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-trust.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

printf 'payload-mm-authenticated-variable-trust-anchor' > "$temporary/content.bin"
printf '%s\n' 'basicConstraints=critical,CA:TRUE' \
	'keyUsage=critical,keyCertSign,cRLSign' > "$temporary/ca.ext"
printf '%s\n' 'basicConstraints=critical,CA:FALSE' \
	'keyUsage=critical,digitalSignature' > "$temporary/signer.ext"

openssl req -new -x509 -newkey rsa:2048 -nodes -days 2 -sha256 \
	-subj /CN=payload-mm-root -addext basicConstraints=critical,CA:TRUE \
	-addext keyUsage=critical,keyCertSign,cRLSign \
	-keyout "$temporary/root.key" -out "$temporary/root.pem" >/dev/null 2>&1
openssl req -new -x509 -newkey rsa:2048 -nodes -days 2 -sha256 \
	-subj /CN=payload-mm-wrong-root -addext basicConstraints=critical,CA:TRUE \
	-addext keyUsage=critical,keyCertSign,cRLSign \
	-keyout "$temporary/wrong-root.key" -out "$temporary/wrong-root.pem" \
	>/dev/null 2>&1
openssl req -new -newkey rsa:2048 -nodes -subj /CN=payload-mm-intermediate \
	-keyout "$temporary/intermediate.key" -out "$temporary/intermediate.csr" \
	>/dev/null 2>&1
openssl x509 -req -in "$temporary/intermediate.csr" -days 1 -sha256 \
	-CA "$temporary/root.pem" -CAkey "$temporary/root.key" -CAcreateserial \
	-extfile "$temporary/ca.ext" -out "$temporary/intermediate.pem" \
	>/dev/null 2>&1

openssl req -new -newkey rsa:2048 -nodes -subj /CN=payload-mm-direct-signer \
	-keyout "$temporary/direct.key" -out "$temporary/direct.csr" >/dev/null 2>&1
openssl x509 -req -in "$temporary/direct.csr" -days 1 -sha256 \
	-CA "$temporary/root.pem" -CAkey "$temporary/root.key" \
	-extfile "$temporary/signer.ext" -out "$temporary/direct.pem" >/dev/null 2>&1
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/direct.pem" -inkey "$temporary/direct.key" -md sha256 \
	-outform DER -out "$temporary/direct.der"

openssl req -new -newkey rsa:2048 -nodes -subj /CN=payload-mm-chain-signer \
	-keyout "$temporary/chain.key" -out "$temporary/chain.csr" >/dev/null 2>&1
openssl x509 -req -in "$temporary/chain.csr" -days 1 -sha256 \
	-CA "$temporary/intermediate.pem" -CAkey "$temporary/intermediate.key" \
	-CAcreateserial -extfile "$temporary/signer.ext" \
	-out "$temporary/chain.pem" >/dev/null 2>&1
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/chain.pem" -inkey "$temporary/chain.key" \
	-certfile "$temporary/intermediate.pem" -md sha256 -outform DER \
	-out "$temporary/intermediate.der"

issuer_cert="$temporary/root.pem"
issuer_key="$temporary/root.key"
: > "$temporary/maximum-chain.pem"
for level in 1 2 3 4 5 6 7; do
	openssl req -new -newkey rsa:2048 -nodes \
		-subj "/CN=payload-mm-maximum-ca-$level" \
		-keyout "$temporary/maximum-ca-$level.key" \
		-out "$temporary/maximum-ca-$level.csr" >/dev/null 2>&1
	openssl x509 -req -in "$temporary/maximum-ca-$level.csr" -days 1 \
		-sha256 -CA "$issuer_cert" -CAkey "$issuer_key" \
		-set_serial "$level" -extfile "$temporary/ca.ext" \
		-out "$temporary/maximum-ca-$level.pem" >/dev/null 2>&1
	cat "$temporary/maximum-ca-$level.pem" >> "$temporary/maximum-chain.pem"
	issuer_cert="$temporary/maximum-ca-$level.pem"
	issuer_key="$temporary/maximum-ca-$level.key"
done
openssl req -new -newkey rsa:2048 -nodes -subj /CN=payload-mm-maximum-signer \
	-keyout "$temporary/maximum.key" -out "$temporary/maximum.csr" \
	>/dev/null 2>&1
openssl x509 -req -in "$temporary/maximum.csr" -days 1 -sha256 \
	-CA "$issuer_cert" -CAkey "$issuer_key" -set_serial 100 \
	-extfile "$temporary/signer.ext" -out "$temporary/maximum.pem" \
	>/dev/null 2>&1
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/maximum.pem" -inkey "$temporary/maximum.key" \
	-certfile "$temporary/maximum-chain.pem" -md sha256 -outform DER \
	-out "$temporary/maximum.der"
openssl verify -CAfile "$temporary/root.pem" \
	-untrusted "$temporary/maximum-chain.pem" "$temporary/maximum.pem" \
	>/dev/null
openssl req -new -x509 -newkey rsa:2048 -nodes -days 2 -sha256 \
	-subj /CN=payload-mm-unrelated -addext basicConstraints=critical,CA:TRUE \
	-addext keyUsage=critical,keyCertSign,cRLSign \
	-keyout "$temporary/unrelated.key" -out "$temporary/unrelated.pem" \
	>/dev/null 2>&1

openssl x509 -in "$temporary/root.pem" -outform DER -out "$temporary/root.der"
openssl x509 -in "$temporary/wrong-root.pem" -outform DER \
	-out "$temporary/wrong-root.der"
openssl x509 -in "$temporary/direct.pem" -outform DER \
	-out "$temporary/direct-cert.der"
openssl x509 -in "$temporary/unrelated.pem" -outform DER \
	-out "$temporary/unrelated.der"

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa sha256 sha512 x509 x509_crt'
mbedtls_sources=
for source in $sources; do
	mbedtls_sources="$mbedtls_sources $root/3rdparty/mbedtls/library/$source.c"
done

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=c11 -O"$optimization" -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections -fsanitize=address,undefined \
		'-DCONFIG(option)=CONFIG_##option' -DPAYLOAD_MM_AUTH_TEST \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY=1 \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR=1 \
		-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
		-I"$root/src/commonlib/bsd/include" -idirafter "$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/lib" \
		-I"$root/src/lib/payload_mm_crypto" \
		-I"$root/3rdparty/mbedtls/include" \
		-I"$root/3rdparty/mbedtls/library" \
		"$root/tests/lib/payload_mm_authvar_trust_anchor_test.c" \
		"$root/src/lib/payload_mm_authvar_trust_anchor.c" \
		"$root/src/lib/payload_mm_crypto/cms.c" \
		"$root/src/lib/payload_mm_crypto/crypto.c" \
		"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" \
		$mbedtls_sources -Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey \
		-o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output" "$temporary"
done

printf 'Payload-MM authvar trust-anchor tests: PASS (%s)\n' "$(openssl version)"
