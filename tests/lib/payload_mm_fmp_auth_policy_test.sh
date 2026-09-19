#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

cases='success dependency dependency-declared dependency-guid
dependency-declared-mismatch dependency-declared-truncated
dependency-guid-truncated dependency-trailing dependency-false
header-extension header-extension-dependency header-small header-overflow
header-no-body reentry source-copy version floor payload-floor
missing-version foreign-board duplicate-fmap
duplicate-build-info verify-failure install-owner install-protection
install-mutation install-authority-mutation install-xdr install-source
install-image-size'

run_test()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_fmp_auth_policy_test.c" \
		"$root/src/lib/payload_mm_fmp_auth_policy.c" -o "$temporary/$name"
	for test_case in $cases; do
		"$temporary/$name" "$test_case"
	done
}

run_test ordinary
run_test optimized -O2
run_test sanitized -O1 -g -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all

if test -n "${PAYLOAD_MM_Q35_ROM:-}"; then
	"$temporary/optimized" board "$PAYLOAD_MM_Q35_ROM" Emulation \
		'QEMU x86 q35/ich9'
fi

# Build a fresh detached CMS and independently verify it before exercising the
# complete policy provider with the same authenticated image and trust root.
"$temporary/optimized" emit "$temporary/payload"
openssl req -new -newkey rsa:2048 -nodes -x509 -sha256 -days 1 \
	-subj /CN=payload-mm-policy-test/ \
	-addext basicConstraints=critical,CA:TRUE \
	-addext keyUsage=critical,digitalSignature,keyCertSign \
	-keyout "$temporary/key.pem" -out "$temporary/cert.pem" 2>/dev/null
openssl x509 -in "$temporary/cert.pem" -outform DER \
	-out "$temporary/cert.der"
cp "$temporary/payload" "$temporary/content"
dd if=/dev/zero bs=8 count=1 status=none >> "$temporary/content"
openssl cms -sign -binary -in "$temporary/content" \
	-signer "$temporary/cert.pem" -inkey "$temporary/key.pem" \
	-outform DER -out "$temporary/cms.der" -nosmimecap
openssl cms -verify -binary -inform DER -in "$temporary/cms.der" \
	-content "$temporary/content" -CAfile "$temporary/cert.pem" \
	-purpose any -no_check_time -partial_chain -out /dev/null 2>/dev/null
cms_size=$(stat -c %s "$temporary/cms.der")
cert_size=$(stat -c %s "$temporary/cert.der")
certificate_length=$((24 + cms_size))
{
	dd if=/dev/zero bs=8 count=1 status=none
	printf '%08x' "$certificate_length" | \
		sed -E 's/(..)(..)(..)(..)/\4\3\2\1/' | xxd -r -p
	printf '0002f10e9dd2af4adf68ee498aa9347d375665a7' | xxd -r -p
	cat "$temporary/cms.der" "$temporary/payload"
} > "$temporary/auth-image"
{
	printf '%08x' "$cert_size" | xxd -r -p
	cat "$temporary/cert.der"
	padding=$(( (4 - cert_size % 4) % 4 ))
	if test "$padding" -ne 0; then
		dd if=/dev/zero bs=1 count="$padding" status=none
	fi
} > "$temporary/trust.xdr"

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa sha256 x509 x509_crt'
objects=
for source in $sources; do
	objects="$objects $root/3rdparty/mbedtls/library/$source.c"
done
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
	-ffunction-sections -fdata-sections \
	-DREAL_AUTH -D__TEST__ -D__COREBOOT__ -D__SMM__ \
	-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/lib" \
	-I"$root/src/lib/payload_mm_crypto" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-idirafter "$root/src/include" \
	-I"$root/src/arch/x86/include" -I"$temporary/include" \
	-I"$root/3rdparty/mbedtls/include" -I"$root/3rdparty/mbedtls/library" \
	"$root/tests/lib/payload_mm_fmp_auth_policy_test.c" \
	"$root/src/lib/payload_mm_fmp_auth_policy.c" \
	"$root/src/lib/payload_mm_crypto/cms.c" \
	"$root/src/lib/payload_mm_crypto/crypto.c" \
	"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" $objects \
	-Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey -o "$temporary/real"
"$temporary/real" real "$temporary/auth-image" "$temporary/trust.xdr"
printf '%s\n' 'Payload-MM FMP authentication policy O0/O2/ASan+UBSan: PASS'
printf 'Payload-MM FMP real CMS/OpenSSL root oracle: PASS (%s)\n' \
	"$(openssl version)"
