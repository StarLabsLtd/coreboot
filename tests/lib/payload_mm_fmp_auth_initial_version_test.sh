#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-fmp-initial-version.XXXXXX")
printf 'Initial-version HOST artifacts: %s\n' "$temporary"
trap 'rm -f "$temporary/trusted.key" "$temporary/rogue.key" "$temporary/signer.pem"' EXIT HUP INT TERM
compiler=${CC:-cc}
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"
common="-std=gnu11 -Wall -Wextra -Werror -fno-builtin -fno-pie -no-pie -ffunction-sections -fdata-sections -D__TEST__ -D__COREBOOT__ -D__SMM__"
includes="-include $root/src/include/kconfig.h -include $root/src/include/rules.h -include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h -I$root/src -I$root/src/lib -I$root/src/lib/payload_mm_crypto -I$root/src/commonlib/include -I$root/src/commonlib/bsd/include -idirafter $root/src/include -I$root/src/arch/x86/include -I$temporary/include"
sanitizers='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
export ASAN_OPTIONS=abort_on_error=1:detect_leaks=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
sha256sum "$root/src/include/payload_mm_fmp_auth_policy.h" \
	"$root/src/lib/payload_mm_fmp_auth_policy.c" \
	"$root/src/mainboard/emulation/qemu-q35/native_service_receiver.c" \
	"$root/tests/lib/payload_mm_fmp_auth_policy_test.c" \
	"$root/tests/lib/payload_mm_fmp_auth_initial_version_test.sh" \
	"$root/util/efi_capsule/generate_capsule.py" \
	"$root/util/efi_capsule/validate_capsule.py" \
	"$root/src/lib/payload_mm_crypto/crypto.c" \
	"$root/src/lib/payload_mm_crypto/cms.c" \
	"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" > "$temporary/source-before.sha256"

# The initial record is modeled; actual policy, CMS and dependency parsing run.
# shellcheck disable=SC2086
"$compiler" $common $includes $sanitizers -O0 \
	"$root/tests/lib/payload_mm_fmp_auth_policy_test.c" \
	"$root/src/lib/payload_mm_fmp_auth_policy.c" -o "$temporary/emit"
"$temporary/emit" emit "$temporary/ordinary.payload"
"$temporary/emit" emit-current-dependency "$temporary/dependency.payload"
dd if="$temporary/ordinary.payload" of="$temporary/rom" bs=1 skip=16 status=none
for signer in trusted rogue; do
	openssl req -new -newkey rsa:2048 -nodes -x509 -sha256 -days 1 \
		-subj "/CN=payload-mm-initial-version-$signer/" \
		-addext basicConstraints=critical,CA:TRUE \
		-addext keyUsage=critical,digitalSignature,keyCertSign \
		-keyout "$temporary/$signer.key" -out "$temporary/$signer.pem" \
		> "$temporary/$signer-create.log" 2>&1
	openssl x509 -in "$temporary/$signer.pem" -outform DER \
		-out "$temporary/$signer.der"
done
perl - "$temporary" <<-'PERL'
	use strict;
	use warnings;
	my ($directory) = @ARGV;
	sub read_bytes {
		my ($name) = @_;
		open my $file, '<:raw', "$directory/$name" or die $!;
		local $/;
		return <$file>;
	}
	sub write_bytes {
		my ($name, $bytes) = @_;
		open my $file, '>:raw', "$directory/$name" or die $!;
		print {$file} $bytes or die $!;
		close $file or die $!;
	}
	write_bytes('signer.pem', read_bytes('trusted.key') . read_bytes('trusted.pem'));
	my $certificate = read_bytes('trusted.der');
	write_bytes('trust.xdr', pack('N', length($certificate)) . $certificate .
		"\0" x ((4 - (length($certificate) & 3)) & 3));
	write_bytes('dependency.content', read_bytes('dependency.payload') . pack('Q<', 7));
PERL
python3 "$root/util/efi_capsule/generate_capsule.py" \
	--output "$temporary/ordinary.cap" --guid 00000001-0000-0000-0000-000000000000 \
	--fw-version 3 --lsv 2 --signer-private-cert "$temporary/signer.pem" \
	--trusted-public-cert "$temporary/trusted.pem" "$temporary/rom"
python3 "$root/util/efi_capsule/validate_capsule.py" \
	--capsule "$temporary/ordinary.cap" --guid 00000001-0000-0000-0000-000000000000 \
	--fw-version 3 --lsv 2 --embedded-drivers 0 --image "$temporary/rom" \
	--require-fmap --trusted-public-cert "$temporary/trusted.pem"
PYTHONPATH="$root" python3 - "$temporary/ordinary.cap" "$temporary/ordinary.image" <<-'PY'
	import pathlib
	import sys
	from util.efi_capsule.validate_capsule import parse_capsule_details
	details = parse_capsule_details(pathlib.Path(sys.argv[1]).read_bytes())
	pathlib.Path(sys.argv[2]).write_bytes(details["authenticated"])
PY
for signer in trusted rogue; do
	openssl cms -sign -binary -in "$temporary/dependency.content" \
		-signer "$temporary/$signer.pem" -inkey "$temporary/$signer.key" \
		-outform DER -out "$temporary/$signer.signature" -md sha256 -nosmimecap
	openssl cms -verify -binary -inform DER -in "$temporary/$signer.signature" \
		-content "$temporary/dependency.content" -CAfile "$temporary/$signer.pem" \
		-purpose any -out /dev/null > "$temporary/$signer-verify.log" 2>&1
done
perl - "$temporary" <<-'PERL'
	use strict;
	use warnings;
	my ($directory) = @ARGV;
	sub read_bytes {
		my ($name) = @_;
		open my $file, '<:raw', "$directory/$name" or die $!;
		local $/;
		return <$file>;
	}
	for my $signer ('trusted', 'rogue') {
		my $signature = read_bytes("$signer.signature");
		open my $file, '>:raw', "$directory/$signer.image" or die $!;
		print {$file} pack('Q<Vvv', 7, 24 + length($signature), 0x200, 0xef1),
			pack('H*', '9dd2af4adf68ee498aa9347d375665a7'), $signature,
			read_bytes('dependency.payload') or die $!;
		close $file or die $!;
	}
PERL

# Each causal changes one expression; the inverse must restore the entire TU.
perl -0777 -pe 's/!record\.present\)/!record.present || !record.data[0])/g == 1 or die "cold guard count"' \
	"$root/src/lib/payload_mm_fmp_auth_policy.c" > "$temporary/refuse-cold.c"
perl -0777 -pe 's/!record\.present \|\| !record\.data\[0\]\)/!record.present)/g == 1 or die "cold inverse count"' \
	"$temporary/refuse-cold.c" | cmp - "$root/src/lib/payload_mm_fmp_auth_policy.c"
perl -0777 -pe 's/auth_policy\.trusted_current_version;/0;/g == 1 or die "baseline count"' \
	"$root/src/lib/payload_mm_fmp_auth_policy.c" > "$temporary/zero-baseline.c"
perl -0777 -pe 's/(installed_version = record\.data\[0\] \? read_le32\(record\.data \+ 4\) :\n\t\t)0;/${1}auth_policy.trusted_current_version;/g == 1 or die "baseline inverse count"' \
	"$temporary/zero-baseline.c" | cmp - "$root/src/lib/payload_mm_fmp_auth_policy.c"
perl -0777 -pe 's/installed_version = record\.data\[0\] \? read_le32\(record\.data \+ 4\) :\n\t\tauth_policy\.trusted_current_version;/installed_version = auth_policy.trusted_current_version;/g == 1 or die "durable count"' \
	"$root/src/lib/payload_mm_fmp_auth_policy.c" > "$temporary/ignore-durable.c"
perl -0777 -pe 's/installed_version = auth_policy\.trusted_current_version;/installed_version = record.data[0] ? read_le32(record.data + 4) :\n\t\tauth_policy.trusted_current_version;/g == 1 or die "durable inverse count"' \
	"$temporary/ignore-durable.c" | cmp - "$root/src/lib/payload_mm_fmp_auth_policy.c"

expect_assertion()
{
	name=$1
	expression=$2
	shift 2
	status=0
	(ulimit -c 0; "$@") > "$temporary/$name.log" 2>&1 || status=$?
	test "$status" -eq 134
	grep -Fq 'Assertion' "$temporary/$name.log"
	grep -Fq "$expression" "$temporary/$name.log"
	if grep -Eq 'AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:' \
		"$temporary/$name.log"; then
		echo "ERROR: sanitizer fault instead of causal assertion: $name" >&2
		exit 1
	fi
}

crypto_sources='asn1parse bignum bignum_core bignum_mod bignum_mod_raw constant_time md oid pk pkparse platform_util rsa sha256 x509 x509_crt'
for optimization in 0 2; do
	objects=
	for source in $crypto_sources; do
		object="$temporary/$source-$optimization.o"
		# shellcheck disable=SC2086
		"$compiler" $common $includes $sanitizers -O"$optimization" \
			-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
			-I"$root/3rdparty/mbedtls/include" -I"$root/3rdparty/mbedtls/library" \
			-c "$root/3rdparty/mbedtls/library/$source.c" -o "$object"
		objects="$objects $object"
	done
	for source in cms crypto mbedtls_verify_wrap; do
		object="$temporary/$source-$optimization.o"
		# shellcheck disable=SC2086
		"$compiler" $common $includes $sanitizers -O"$optimization" \
			-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
			-I"$root/3rdparty/mbedtls/include" -I"$root/3rdparty/mbedtls/library" \
			-c "$root/src/lib/payload_mm_crypto/$source.c" -o "$object"
		objects="$objects $object"
	done
	# shellcheck disable=SC2086
	"$compiler" $common $includes $sanitizers -O"$optimization" -DREAL_AUTH \
		-c "$root/tests/lib/payload_mm_fmp_auth_policy_test.c" \
		-o "$temporary/fixture-$optimization.o"
	objects="$objects $temporary/fixture-$optimization.o"
	for variant in positive refuse-cold zero-baseline ignore-durable; do
		policy="$root/src/lib/payload_mm_fmp_auth_policy.c"
		if test "$variant" != positive; then policy="$temporary/$variant.c"; fi
		# shellcheck disable=SC2086
		"$compiler" $common $includes $sanitizers -O"$optimization" \
			-c "$policy" -o "$temporary/$variant-$optimization.o"
		# shellcheck disable=SC2086
		"$compiler" $common $sanitizers -Wl,--gc-sections,--wrap=mbedtls_rsa_parse_pubkey \
			$objects "$temporary/$variant-$optimization.o" \
			-o "$temporary/$variant-$optimization"
	done
	positive="$temporary/positive-$optimization"
	for mode in real real-cold; do
		"$positive" "$mode" "$temporary/ordinary.image" "$temporary/trust.xdr" \
			> "$temporary/$mode-$optimization.log" 2>&1
	done
	for mode in real-cold-dependency real-cold-floor real-cold-absent \
		real-cold-owner-error real-cold-drift real-cold-aba real-present-dependency \
		real-cold-source-copy real-cold-unflagged-data; do
		"$positive" "$mode" "$temporary/trusted.image" "$temporary/trust.xdr" \
			> "$temporary/$mode-$optimization.log" 2>&1
	done
	"$positive" real-cold-rogue "$temporary/rogue.image" "$temporary/trust.xdr" \
		> "$temporary/real-cold-rogue-$optimization.log" 2>&1
	expect_assertion "refuse-cold-$optimization" 'status == CB_SUCCESS' \
		"$temporary/refuse-cold-$optimization" real-cold \
		"$temporary/ordinary.image" "$temporary/trust.xdr"
	expect_assertion "zero-baseline-$optimization" 'status == CB_SUCCESS' \
		"$temporary/zero-baseline-$optimization" real-cold-dependency \
		"$temporary/trusted.image" "$temporary/trust.xdr"
	expect_assertion "ignore-durable-$optimization" 'status == CB_ERR' \
		"$temporary/ignore-durable-$optimization" real-present-dependency \
		"$temporary/trusted.image" "$temporary/trust.xdr"
done
sha256sum -c "$temporary/source-before.sha256"
printf '%s\n' 'Initial-version HOST actual CMS/dependencies and six exact causals: PASS'
