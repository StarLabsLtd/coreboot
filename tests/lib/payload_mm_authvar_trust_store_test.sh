#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mbedtls_root="$root/3rdparty/mbedtls"
if [ ! -f "$mbedtls_root/library/x509_crt.c" ]; then
	common_dir=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)
	mbedtls_root=$(CDPATH= cd -- "$(dirname -- "$common_dir")/3rdparty/mbedtls" && pwd)
fi
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-trust-store.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

printf 'payload-mm-authenticated-variable-trust-store' > "$temporary/content.bin"
printf '%s\n' 'basicConstraints=critical,CA:FALSE' \
	'keyUsage=critical,digitalSignature' > "$temporary/signer.ext"

make_ca()
{
	name=$1
	openssl req -new -x509 -newkey rsa:2048 -nodes -days 2 -sha256 \
		-subj "/CN=payload-mm-$name" \
		-addext basicConstraints=critical,CA:TRUE \
		-addext keyUsage=critical,keyCertSign,cRLSign \
		-keyout "$temporary/$name.key" -out "$temporary/$name.pem" \
		>/dev/null 2>&1
	openssl x509 -in "$temporary/$name.pem" -outform DER \
		-out "$temporary/$name.der"
}

make_signer()
{
	name=$1
	issuer=$2
	openssl req -new -newkey rsa:2048 -nodes \
		-subj "/CN=payload-mm-$name-signer" \
		-keyout "$temporary/$name-signer.key" \
		-out "$temporary/$name-signer.csr" >/dev/null 2>&1
	openssl x509 -req -in "$temporary/$name-signer.csr" -days 1 -sha256 \
		-CA "$temporary/$issuer.pem" -CAkey "$temporary/$issuer.key" \
		-CAcreateserial -extfile "$temporary/signer.ext" \
		-out "$temporary/$name-signer.pem" >/dev/null 2>&1
}

make_ca parent
make_ca kek
make_ca wrong
printf '%s\n' 'basicConstraints=critical,CA:TRUE' \
	'keyUsage=critical,digitalSignature,keyCertSign,cRLSign' > \
	"$temporary/pk.ext"
openssl req -new -newkey rsa:2048 -nodes -subj /CN=payload-mm-current-pk \
	-keyout "$temporary/pk.key" -out "$temporary/pk.csr" >/dev/null 2>&1
openssl x509 -req -in "$temporary/pk.csr" -days 1 -sha256 \
	-CA "$temporary/parent.pem" -CAkey "$temporary/parent.key" \
	-CAcreateserial -extfile "$temporary/pk.ext" -out "$temporary/pk.pem" \
	>/dev/null 2>&1
openssl x509 -in "$temporary/pk.pem" -outform DER -out "$temporary/pk.der"
make_signer pk pk
make_signer kek kek

openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/pk.pem" -inkey "$temporary/pk.key" \
	-certfile "$temporary/parent.pem" -md sha256 -outform DER \
	-out "$temporary/pk-embedded.der"
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/pk-signer.pem" -inkey "$temporary/pk-signer.key" \
	-md sha256 -outform DER -out "$temporary/pk-omitted.der"
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/pk-signer.pem" -inkey "$temporary/pk-signer.key" \
	-certfile "$temporary/pk.pem" -md sha256 -outform DER \
	-out "$temporary/pk-chain-no-parent.der"
( openssl x509 -in "$temporary/pk.pem"; \
	openssl x509 -in "$temporary/parent.pem" ) > "$temporary/pk-chain.pem"
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/pk-signer.pem" -inkey "$temporary/pk-signer.key" \
	-certfile "$temporary/pk-chain.pem" -md sha256 -outform DER \
	-out "$temporary/pk-chain.der"
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/kek-signer.pem" -inkey "$temporary/kek-signer.key" \
	-certfile "$temporary/kek.pem" -md sha256 -outform DER \
	-out "$temporary/kek-embedded.der"

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa sha256 sha512 x509 x509_crt'
mbedtls_sources=
for source in $sources; do
	mbedtls_sources="$mbedtls_sources $mbedtls_root/library/$source.c"
done

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-DPAYLOAD_MM_AUTH_TEST \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY=1 \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR=1 \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_TRUST_STORE=1 \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_SIGNATURE_DB=1 \
		-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
		-I"$temporary/include" -I"$root/src" \
		-I"$root/src/commonlib/bsd/include" -idirafter "$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/lib" \
		-I"$root/src/arch/x86/include" \
		-I"$root/src/lib/payload_mm_crypto" \
		-I"$mbedtls_root/include" \
		-I"$mbedtls_root/library" \
		"$root/tests/lib/payload_mm_authvar_trust_store_test.c" \
		"$root/src/lib/payload_mm_authvar_trust_store.c" \
		"$root/src/lib/payload_mm_authvar_signature_db.c" \
		"$root/src/lib/payload_mm_authvar_trust_anchor.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_crypto/cms.c" \
		"$root/src/lib/payload_mm_crypto/crypto.c" \
		"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" \
		$mbedtls_sources -Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey \
		-o "$output"
	ASAN_OPTIONS=detect_leaks=1 "$output" "$temporary"
done

printf 'Payload-MM authvar trust-store tests: PASS (%s)\n' "$(openssl version)"
