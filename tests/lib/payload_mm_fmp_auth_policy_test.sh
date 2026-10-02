#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
sha256sum "$root/src/lib/payload_mm_fmp_auth_policy.c" \
	"$root/tests/lib/payload_mm_fmp_auth_policy_test.c" > "$temporary/inputs.sha256"
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"
printf '%s\n' '#include <stdint.h>' 'typedef uint8_t u8;' \
	'typedef uint32_t u32;' 'typedef uint64_t u64;' > \
	"$temporary/include/coreboot-integer-types.h"

cases='success dependency dependency-declared dependency-guid
dependency-declared-mismatch dependency-declared-truncated
dependency-guid-truncated dependency-trailing dependency-false
header-extension header-extension-dependency header-small header-overflow
header-no-body reentry source-copy version floor payload-floor
missing-version cold-dependency cold-source-copy baseline-zero baseline-below-floor
absent-record owner-error
foreign-board duplicate-fmap
duplicate-build-info verify-failure install-owner install-protection
install-mutation install-current-mutation install-authority-mutation install-xdr install-source
install-image-size owner-same-sequence owner-aba'

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
	-fno-pie -no-pie \
	-fsanitize=address,undefined -fno-sanitize-recover=all

if test -n "${PAYLOAD_MM_Q35_ROM:-}"; then
	"$temporary/optimized" board "$PAYLOAD_MM_Q35_ROM" Emulation \
		'QEMU x86 q35/ich9'
fi

# Build a complete capsule with the production generator before exercising the
# complete policy provider with its exact authenticated image and trust root.
"$temporary/optimized" emit "$temporary/payload"
dd if="$temporary/payload" of="$temporary/raw-rom" bs=1 skip=16 status=none
openssl req -new -newkey rsa:2048 -nodes -x509 -sha256 -days 1 \
	-subj /CN=payload-mm-policy-test/ \
	-addext basicConstraints=critical,CA:TRUE \
	-addext keyUsage=critical,digitalSignature,keyCertSign \
	-keyout "$temporary/key.pem" -out "$temporary/cert.pem" 2>/dev/null
openssl x509 -in "$temporary/cert.pem" -outform DER \
	-out "$temporary/cert.der"
cat "$temporary/key.pem" "$temporary/cert.pem" > "$temporary/signer.pem"
python3 "$root/util/efi_capsule/generate_capsule.py" \
	--output "$temporary/coreboot.cap" \
	--guid 975cd0e6-c540-4e2b-906c-72c0d0d1e40d \
	--fw-version 3 --lsv 2 \
	--signer-private-cert "$temporary/signer.pem" \
	--trusted-public-cert "$temporary/cert.pem" \
	"$temporary/raw-rom"
PYTHONPATH="$root" python3 - "$temporary/coreboot.cap" \
	"$temporary/auth-image" <<'PY'
import pathlib
import sys

from util.efi_capsule.validate_capsule import parse_capsule_details

details = parse_capsule_details(pathlib.Path(sys.argv[1]).read_bytes())
pathlib.Path(sys.argv[2]).write_bytes(details["authenticated"])
PY
cert_size=$(stat -c %s "$temporary/cert.der")
{
	printf '%08x' "$cert_size" | xxd -r -p
	cat "$temporary/cert.der"
	padding=$(( (4 - cert_size % 4) % 4 ))
	if test "$padding" -ne 0; then
		dd if=/dev/zero bs=1 count="$padding" status=none
	fi
} > "$temporary/trust.xdr"

openssl req -new -newkey rsa:2048 -nodes -x509 -sha256 -days 1 \
	-subj /CN=payload-mm-untrusted-signer/ \
	-addext basicConstraints=critical,CA:TRUE \
	-addext keyUsage=critical,digitalSignature,keyCertSign \
	-keyout "$temporary/other-key.pem" -out "$temporary/other-cert.pem" 2>/dev/null
openssl x509 -in "$temporary/other-cert.pem" -outform DER \
	-out "$temporary/other-cert.der"
cert_size=$(stat -c %s "$temporary/other-cert.der")
{
	printf '%08x' "$cert_size" | xxd -r -p
	cat "$temporary/other-cert.der"
	padding=$(( (4 - cert_size % 4) % 4 ))
	if test "$padding" -ne 0; then
		dd if=/dev/zero bs=1 count="$padding" status=none
	fi
} > "$temporary/other-trust.xdr"

sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time
md oid pk pkparse platform_util rsa sha256 x509 x509_crt'
objects=
for source in $sources; do
	objects="$objects $root/3rdparty/mbedtls/library/$source.c"
done
compile_real()
{
	policy_source=$1
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
		"$policy_source" \
		"$root/src/lib/payload_mm_crypto/cms.c" \
		"$root/src/lib/payload_mm_crypto/crypto.c" \
		"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" $objects \
		-Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey,--wrap=payload_mm_crypto_end \
		-o "$temporary/real"
}
compile_real "$root/src/lib/payload_mm_fmp_auth_policy.c"
"$temporary/real" real "$temporary/auth-image" "$temporary/trust.xdr"
"$temporary/real" real-cold-rogue "$temporary/auth-image" "$temporary/other-trust.xdr"
for test_case in real-cold-corrupt real-cold-corrupt-cleanup \
		 real-cold-corrupt-allocation real-cold-corrupt-dirty; do
	"$temporary/real" "$test_case" "$temporary/auth-image" "$temporary/trust.xdr"
done
for guard in final-status clean-owner; do
	python3 - "$root/src/lib/payload_mm_fmp_auth_policy.c" \
		"$temporary/policy-$guard.c" "$guard" <<'PY'
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_text()
guard = ("verified == PAYLOAD_MM_VERIFY_REJECTED &&" if sys.argv[3] == "final-status"
         else "payload_mm_crypto_owner_is_clean(&auth_policy.crypto)")
replacement = ("1 /* final status discarded */ &&" if sys.argv[3] == "final-status"
               else "1 /* clean owner discarded */")
assert source.count(guard) == 1
mutant = source.replace(guard, replacement)
assert mutant.replace(replacement, guard, 1) == source
pathlib.Path(sys.argv[2]).write_text(mutant)
PY
	compile_real "$temporary/policy-$guard.c"
	test_case=real-cold-corrupt-allocation
	if test "$guard" = clean-owner; then
		test_case=real-cold-corrupt-dirty
	fi
	set +e
	"$temporary/real" "$test_case" "$temporary/auth-image" \
		"$temporary/trust.xdr" > "$temporary/$guard.log" 2>&1
	status=$?
	set -e
	if test "$status" -ne 134 ||
	   ! grep -F 'main: Assertion `status == ((!strcmp(argv[1], "real-cold-rogue") ||' \
		"$temporary/$guard.log" >/dev/null; then
		cat "$temporary/$guard.log" >&2
		exit 1
	fi
	if grep -E 'AddressSanitizer|LeakSanitizer|runtime error:' "$temporary/$guard.log"; then
		exit 1
	fi
	printf 'Payload-MM signature refusal %s guard cause: targeted assertion 134\n' "$guard"
done
"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -fno-builtin \
	-ffunction-sections -fdata-sections \
	-DREAL_AUTH -D__TEST__ -D__COREBOOT__ -D__SMM__ \
	-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-include "$temporary/include/coreboot-integer-types.h" \
	-I"$root/src" -I"$root/src/lib" \
	-I"$root/src/lib/payload_mm_crypto" \
	-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
	-idirafter "$root/src/include" \
	-I"$root/src/arch/x86/include" -I"$temporary/include" \
	-I"$root/3rdparty/mbedtls/include" -I"$root/3rdparty/mbedtls/library" \
	"$root/tests/lib/payload_mm_fmp_capsule_composed_test.c" \
	"$root/src/lib/payload_mm_fmp_auth_policy.c" \
	"$root/src/lib/capsule_broker.c" \
	"$root/src/lib/capsule_broker_endpoint.c" \
	"$root/src/lib/payload_mm_fmp_owner_layout.c" \
	"$root/src/lib/capsule_write_layout.c" \
	"$root/src/lib/capsule_update_backend.c" \
	"$root/src/lib/payload_mm_crypto/cms.c" \
	"$root/src/lib/payload_mm_crypto/crypto.c" \
	"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" $objects \
	-Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey \
	-o "$temporary/composed"
"$temporary/composed" "$temporary/auth-image" "$temporary/trust.xdr" \
	"$temporary/raw-rom"
printf '%s\n' 'Payload-MM FMP authentication policy O0/O2/ASan+UBSan: PASS'
printf 'Payload-MM FMP real CMS/OpenSSL root oracle: PASS (%s)\n' \
	"$(openssl version)"
printf '%s\n' 'Payload-MM FMP signed-envelope broker composition: PASS'
sha256sum -c "$temporary/inputs.sha256"
