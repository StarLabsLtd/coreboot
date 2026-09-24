#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mbedtls_root="$root/3rdparty/mbedtls"
if [ ! -f "$mbedtls_root/library/x509_crt.c" ]; then
	common_dir=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)
	mbedtls_root=$(CDPATH= cd -- "$(dirname -- "$common_dir")/3rdparty/mbedtls" && pwd)
fi
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-private-trust.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"
printf 'P\000r\000i\000v\000\020\062\124\166\230\272\334\376\001\043\105\147\211\253\315\357\041\000\000\000\352\007\001\001\000\000\001\000\000\000\000\000\000\000\000\000payload' \
	> "$temporary/content.bin"
dd if="$temporary/content.bin" of="$temporary/empty-content.bin" bs=1 count=44 \
	status=none
cp "$temporary/empty-content.bin" "$temporary/append-empty-content.bin"
printf '\141' | dd of="$temporary/append-empty-content.bin" bs=1 seek=24 \
	conv=notrunc status=none

make_signer()
{
	name=$1
	subject=$2
	openssl req -new -x509 -newkey rsa:2048 -nodes -days 2 -sha256 \
		-subj "$subject" -addext basicConstraints=critical,CA:FALSE \
		-addext keyUsage=critical,digitalSignature \
		-keyout "$temporary/$name.key" -out "$temporary/$name.pem" \
		>/dev/null 2>&1
	openssl x509 -in "$temporary/$name.pem" -outform DER \
		-out "$temporary/$name-cert.der"
}

make_signer signer /CN=payload-mm-private-signer
make_signer wrong /CN=payload-mm-wrong-signer
make_signer no-cn /O=payload-mm-no-common-name
for algorithm in sha256 sha384 sha512; do
	openssl cms -sign -binary -in "$temporary/content.bin" \
		-signer "$temporary/signer.pem" -inkey "$temporary/signer.key" \
		-md "$algorithm" -outform DER -out "$temporary/$algorithm.der"
done
openssl cms -sign -binary -in "$temporary/empty-content.bin" \
	-signer "$temporary/signer.pem" -inkey "$temporary/signer.key" \
	-md sha256 -outform DER -out "$temporary/empty-sha256.der"
openssl cms -sign -binary -in "$temporary/append-empty-content.bin" \
	-signer "$temporary/signer.pem" -inkey "$temporary/signer.key" \
	-md sha256 -outform DER -out "$temporary/append-empty-sha256.der"
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/wrong.pem" -inkey "$temporary/wrong.key" \
	-md sha256 -outform DER -out "$temporary/wrong.der"
openssl cms -sign -binary -in "$temporary/empty-content.bin" \
	-signer "$temporary/no-cn.pem" -inkey "$temporary/no-cn.key" \
	-md sha256 -outform DER -out "$temporary/no-cn.der"
openssl cms -sign -binary -in "$temporary/append-empty-content.bin" \
	-signer "$temporary/no-cn.pem" -inkey "$temporary/no-cn.key" \
	-md sha256 -outform DER -out "$temporary/no-cn-append.der"
openssl cms -sign -binary -in "$temporary/content.bin" \
	-signer "$temporary/no-cn.pem" -inkey "$temporary/no-cn.key" \
	-md sha256 -outform DER -out "$temporary/no-cn-full.der"

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa rsa_alt_helpers sha256 sha512 x509 x509_crt'
mbedtls_sources=
for source in $sources; do
	mbedtls_sources="$mbedtls_sources $mbedtls_root/library/$source.c"
done

compile_test()
{
	trust_source=$1
	output=$2
	optimization=$3
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-DPAYLOAD_MM_AUTH_TEST -DCONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY=1 \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING=1 \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST=1 \
		-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
		-I"$temporary/include" -I"$root/src" \
		-I"$root/src/commonlib/bsd/include" -idirafter "$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/lib" \
		-I"$root/src/arch/x86/include" -I"$root/src/lib/payload_mm_crypto" \
		-I"$mbedtls_root/include" -I"$mbedtls_root/library" \
		"$root/tests/lib/payload_mm_authvar_private_trust_test.c" \
		"$trust_source" "$root/src/lib/payload_mm_authvar_private_binding.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_crypto/cms.c" \
		"$root/src/lib/payload_mm_crypto/crypto.c" $mbedtls_sources \
		"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" \
		-Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey -o "$output"
}

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	compile_test "$root/src/lib/payload_mm_authvar_private_trust.c" "$output" \
		"$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$output" "$temporary"
done

mutant_test()
{
	label=$1
	expression=$2
	mutant="$temporary/$label.c"
	cp "$root/src/lib/payload_mm_authvar_private_trust.c" "$mutant"
	perl -0pi -e "$expression" "$mutant"
	if cmp -s "$root/src/lib/payload_mm_authvar_private_trust.c" "$mutant"; then
		echo "ERROR: $label mutant did not change source" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/$label-O$optimization"
		compile_test "$mutant" "$output" "$optimization"
		if ASAN_OPTIONS=detect_leaks=1 "$output" "$temporary" \
			>/dev/null 2>&1; then
			echo "ERROR: $label O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant_test skip-empty \
	's/if \(!needs_certdb\)/if (!needs_certdb \&\& false)/'
mutant_test skip-absence \
	's/if \(lookup != PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND\)/if (false)/'
mutant_test skip-existing-match \
	's/status = payload_mm_authvar_private_binding_match\(owner,\n\t\t\t&signed_data_copy, &verified, &stored_span\);/(void)stored_span; status = PAYLOAD_MM_VERIFY_OK;/'
mutant_test wrong-authority \
	's/draft\.accepted_authority =\n\t\t\tPAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;/draft.accepted_authority =\n\t\t\tPAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;/'
mutant_test skip-input-seal \
	's/memcmp\(&before, &after, sizeof\(before\)\)/false/'
mutant_test skip-clean \
	's/payload_mm_crypto_owner_is_clean\(owner\)/true/'

printf 'Payload-MM authvar private-trust tests: PASS (%s)\n' "$(openssl version)"
