#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mbedtls_root="$root/3rdparty/mbedtls"
if [ ! -f "$mbedtls_root/library/x509_crt.c" ]; then
	common_dir=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir)
	mbedtls_root=$(CDPATH= cd -- "$(dirname -- "$common_dir")/3rdparty/mbedtls" && pwd)
fi
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-private-binding.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

base64 -d "$root/tests/data/payload_mm_crypto/fixtures/cms.der.b64" \
	> "$temporary/cms.der"
openssl pkcs7 -inform DER -in "$temporary/cms.der" -print_certs \
	-out "$temporary/certificates.pem"
csplit -s -f "$temporary/certificate-" -b '%02d.pem' \
	"$temporary/certificates.pem" '/-----BEGIN CERTIFICATE-----/' '{*}'
openssl x509 -in "$temporary/certificate-01.pem" -outform DER \
	-out "$temporary/intermediate.der"
openssl x509 -in "$temporary/certificate-02.pem" -outform DER \
	-out "$temporary/signer.der"
openssl req -new -x509 -newkey rsa:2048 -nodes -days 1 \
	-subj '/CN=FIRST/CN=SECOND' -keyout "$temporary/duplicate.key" \
	-out "$temporary/duplicate.pem" >/dev/null 2>&1
openssl x509 -in "$temporary/duplicate.pem" -outform DER \
	-out "$temporary/duplicate.der"

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa rsa_alt_helpers sha256 sha512 x509 x509_crt'
mbedtls_sources=
for source in $sources; do
	mbedtls_sources="$mbedtls_sources $mbedtls_root/library/$source.c"
done

compile_test()
{
	source=$1
	output=$2
	optimization=$3
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-ffunction-sections -fdata-sections -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-DPAYLOAD_MM_AUTH_TEST -DCONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY=1 \
		-DCONFIG_PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING=1 \
		-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
		-I"$temporary/include" -I"$root/src" \
		-I"$root/src/commonlib/bsd/include" -idirafter "$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/lib" \
		-I"$root/src/arch/x86/include" -I"$root/src/lib/payload_mm_crypto" \
		-I"$mbedtls_root/include" -I"$mbedtls_root/library" \
		"$root/tests/lib/payload_mm_authvar_private_binding_test.c" "$source" \
		"$root/src/lib/payload_mm_crypto/crypto.c" $mbedtls_sources \
		"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" \
		-Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey -o "$output"
}

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	compile_test "$root/src/lib/payload_mm_authvar_private_binding.c" \
		"$output" "$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$output" "$temporary/intermediate.der" \
		"$temporary/signer.der" "$temporary/duplicate.der"
done

mutant_test()
{
	label=$1
	expression=$2
	mutant="$temporary/$label.c"
	cp "$root/src/lib/payload_mm_authvar_private_binding.c" "$mutant"
	perl -0pi -e "$expression" "$mutant"
	if cmp -s "$root/src/lib/payload_mm_authvar_private_binding.c" "$mutant"; then
		echo "ERROR: $label mutant did not change source" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/$label-O$optimization"
		log="$temporary/$label-O$optimization.log"
		if ! compile_test "$mutant" "$output" "$optimization" >"$log" 2>&1; then
			echo "ERROR: $label O$optimization mutant did not compile" >&2
			cat "$log" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$output" \
			"$temporary/intermediate.der" "$temporary/signer.der" \
			"$temporary/duplicate.der" >"$log" 2>&1; then
			echo "ERROR: $label O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant_test fixed-algorithm \
	's/algorithm = verified_snapshot\.digest_algorithm;/algorithm = PAYLOAD_MM_HASH_SHA256;/'
mutant_test signer-as-intermediate \
	's/verified_snapshot\.signer_certificate/verified_snapshot.certificates[0]/g'
mutant_test whole-certificate \
	's/tbs\.data = signer\.tbs\.p;\n\ttbs\.size = signer\.tbs\.len;/tbs.data = signer.raw.p;\n\ttbs.size = signer.raw.len;/'
mutant_test intermediate-tbs \
	's/tbs\.data = signer\.tbs\.p;\n\ttbs\.size = signer\.tbs\.len;/tbs.data = verified_snapshot.certificates[0].data + 4U;\n\ttbs.size = 536U;/'
mutant_test tbs-value-only \
	's/tbs\.data = signer\.tbs\.p;\n\ttbs\.size = signer\.tbs\.len;/tbs.data = signer.tbs.p + 4U;\n\ttbs.size = signer.tbs.len - 4U;/'
mutant_test skip-first-cn \
	's/for \(name = &signer\.subject;/for (name = signer.subject.next;/'
mutant_test select-last-cn \
	's/MBEDTLS_OID_CMP\(MBEDTLS_OID_AT_CN, &name->oid\) == 0/MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, \&name->oid) == 0 \&\& name->next == NULL/'
mutant_test hash-past-nul \
	's/for \(spans\[0\]\.size = 0U; spans\[0\]\.size < converted_size; spans\[0\]\.size\+\+\)\n\t\tif \(!converted\[spans\[0\]\.size\]\)\n\t\t\tbreak;/spans[0].size = converted_size;/'
mutant_test truncate-126 \
	's/MIN\(converted_size, EDK_PRIVATE_CN_CAPACITY\)/MIN(converted_size, EDK_PRIVATE_CN_CAPACITY - 1U)/'
mutant_test legacy-as-digest \
	's/if \(existing->size == digest_size\)/if (existing->size == digest_size || existing->size == LEGACY_CERT_STACK_HEADER_SIZE + verified->signer_certificate.size)/'
mutant_test omit-verified-seal \
	's/memcmp\(verified, &verified_snapshot, sizeof\(\*verified\)\)/false/g'
mutant_test omit-content-seal \
	's/memcmp\(input_digest, check_digest, sizeof\(input_digest\)\)/false/g'
mutant_test omit-span-seal \
	's/memcmp\(signed_data, &signed_data_snapshot, sizeof\(\*signed_data\)\)/false/g'
mutant_test omit-failure-restore \
	's/\*binding = original;\n\t\treturn status;/return status;/'

printf 'Payload-MM authvar private-binding tests: PASS (%s)\n' "$(openssl version)"
