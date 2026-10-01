#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
runtime_wave=${PROVIDER_STACK_RUNTIME_WAVE:-0}
case "$runtime_wave" in 0|1) ;; *) exit 1 ;; esac
delivery_lane=${PROVIDER_STACK_BOOT_PRIVATE_DELIVERY:-0}
case "$delivery_lane" in 0|1) ;; *) exit 1 ;; esac
[ "$runtime_wave" -eq 0 ] || [ "$delivery_lane" -eq 0 ]
fixture_source="$root/tests/lib/payload_mm_authvar_service_provider_stack_test.c"
mbedtls_source=${MBEDTLS_SOURCE:-$root/3rdparty/mbedtls}
temporary=$(mktemp -d)
trap 'if [ "${KEEP_PROVIDER_STACK_TMP:-0}" = 1 ]; then
	printf "Provider stack artifacts: %s\n" "$temporary" >&2
else
	rm -rf "$temporary"
fi' EXIT HUP INT TERM
mkdir "$temporary/include"
if [ "$runtime_wave" -eq 1 ]; then
	fixture_source="$root/tests/mainboard/starlabs/starbook_mtl_authvar_service_runtime_wave_test.c"
	mkdir -p "$temporary/include/arch" "$temporary/include/soc" \
		"$temporary/include/cpu/x86" "$temporary/include/device" \
		"$temporary/include/intelblocks"
	printf '%s\n' '#pragma once' '#include <stdint.h>' \
		'typedef struct { uint32_t lo, hi; } msr_t;' \
		'msr_t rdmsr(unsigned int index);' > "$temporary/include/cpu/x86/msr.h"
	printf '%s\n' '#pragma once' 'unsigned long tsc_freq_mhz(void);' \
		> "$temporary/include/cpu/x86/tsc.h"
	printf '%s\n' '#pragma once' '#include <stdint.h>' '#include <types.h>' \
		'#include <device/pci_type.h>' \
		'uint32_t pci_read_config32(unsigned int device, unsigned int index);' \
		> "$temporary/include/device/pci_ops.h"
	printf '%s\n' '#pragma once' '#define TSEG 0xb8' '#define BGSM 0xb4' \
		> "$temporary/include/intelblocks/systemagent.h"
	printf '%s\n' '#define SA_DEV_ROOT 0' > "$temporary/include/soc/pci_devs.h"
	printf '%s\n' '#include <stdint.h>' 'uint8_t inb(uint16_t port);' \
		'uint32_t inl(uint16_t port);' > "$temporary/include/arch/io.h"
	printf '%s\n' '#define APM_STS_BIT 5' '#define ACPI_BASE_ADDRESS 0x1800' \
		'#define SMI_STS 0x34' > "$temporary/include/soc/pm.h"
fi
printf '%s\n' '#include <stdint.h>' \
	'#include <commonlib/helpers.h>' '#define DEVTREE_CONST const' \
	'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;' \
	'typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;' \
	> "$temporary/include/host-types.h"
printf 'P\000r\000i\000v\000\020\062\124\166\230\272\334\376\001\043\105\147\211\253\315\357\043\000\000\000\352\007\012\001\000\000\001\000\000\000\000\000\000\000\000\000payload' \
	> "$temporary/content.bin"
openssl req -new -x509 -newkey rsa:2048 -nodes -days 2 -sha256 \
	-subj /CN=payload-mm-provider-signer -addext basicConstraints=critical,CA:FALSE \
	-addext keyUsage=critical,digitalSignature -keyout "$temporary/signer.key" \
	-out "$temporary/signer.pem" >/dev/null 2>&1
openssl cms -sign -binary -in "$temporary/content.bin" -signer "$temporary/signer.pem" \
	-inkey "$temporary/signer.key" -md sha256 -outform DER -out "$temporary/signed.der"
openssl x509 -in "$temporary/signer.pem" -outform DER -out "$temporary/signer.der"
openssl asn1parse -inform DER -in "$temporary/signer.der" > "$temporary/signer-asn1.txt"
set -- $(sed -n '2s/^ *\([0-9]*\):.*hl= *\([0-9]*\) l= *\([0-9]*\).*$/\1 \2 \3/p' \
	"$temporary/signer-asn1.txt")
test "$#" -eq 3
{
	printf '%s' payload-mm-provider-signer
	dd if="$temporary/signer.der" bs=1 skip="$1" count="$(($2 + $3))" status=none
} | openssl dgst -sha256 -binary > "$temporary/expected-binding.bin"
perl -0777 -pe 'substr($_, 0, 1) = chr(ord(substr($_, 0, 1)) ^ 1)' \
	"$temporary/expected-binding.bin" > "$temporary/wrong-binding.bin"
perl -e 'open my $input, "<", $ARGV[0] or die $!; binmode $input;
local $/; my $cms = <$input>;
print pack("vC6VvC2", 2026, 10, 1, 0, 0, 1, 0, 0, 0, 0, 0);
print pack("Vvv", 24 + length($cms), 0x200, 0xef1);
print pack("H*", "9dd2af4adf68ee498aa9347d375665a7"), $cms, "payload";' \
	"$temporary/signed.der" > "$temporary/auth2.bin"
for authentication in auth2 ordinary; do
	if [ "$delivery_lane" -eq 1 ] && [ "$authentication" = auth2 ]; then
		continue
	fi
	# The baseline source/config is identical: exercise both argv profiles once built.
	if [ "$runtime_wave" -eq 1 ] && [ "$authentication" = ordinary ]; then
		continue
	fi
	profiles='0:65536 0:4096'
	if [ "$delivery_lane" -eq 1 ]; then
		profiles='0:65536 0:4096 1:65536 1:4096'
	fi
	for profile in $profiles; do
		private_buffer=${profile%:*}
		block_size=${profile#*:}
		printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
			'#define CONFIG_MAX_CPUS 4' '#define CONFIG_SMMSTORE 0' \
			'#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS 0' \
			"#define CONFIG_SMMSTORE_BLOCK_SIZE $block_size" \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_CMS_VERIFY 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRIVATE_BINDING 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRIVATE_TRUST 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_TRUST_ANCHOR 1' \
			'#define CONFIG_PAYLOAD_MM_AUTHVAR_REQUIRE_SELF_SIGNED_PK 0' \
			'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 0' \
			'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' > "$temporary/include/config.h"
		printf '#define CONFIG_PAYLOAD_BOOT_PRIVATE_BUFFER %s\n' "$private_buffer" \
			>> "$temporary/include/config.h"
		if [ "$runtime_wave" -eq 1 ]; then
			printf '%s\n' '#define CONFIG_ROM_SIZE 8388608' \
				'#define CONFIG_SMM_INVOCATION_EVIDENCE 1' \
				'#define CONFIG_SMM_INVOCATION_ENTRY 1' \
				'#define CONFIG_SMM_INVOCATION_FAIL_STOP_PLATFORM 1' \
				'#define CONFIG_SMM_INVOCATION_TOPOLOGY 1' \
				'#define CONFIG_SMM_INVOCATION_LOADER_INSTANCE 1' \
				'#define CONFIG_SMM_INVOCATION_LOADER_COMPOSITION 1' \
				'#define CONFIG_SMM_INVOCATION_INTEL_ADAPTER_PROVIDER 1' \
				'#define CONFIG_SMM_APMC_COMMAND_REGISTRY 1' \
				'#define CONFIG_SMM_APMC_COMPOSITION_ATTESTED 1' \
				'#define CONFIG_SMM_APMC_ROUTE_AUTHVAR_SERVICE 1' \
				>> "$temporary/include/config.h"
		fi
		variants='baseline record-capacity header-capacity no-request-scrub early-release early-completion'
		if [ "$delivery_lane" -eq 1 ]; then
			variants=baseline
			if [ "$private_buffer" -eq 1 ]; then
				variants='baseline begin-order recheck-order no-begin no-recheck'
			fi
		fi
		for variant in $variants; do
			if [ "$delivery_lane" -eq 1 ] && [ "$block_size" -eq 4096 ] &&
			   [ "$variant" != baseline ]; then
				continue
			fi
			if [ "$runtime_wave" -eq 1 ] && [ "$variant" != baseline ]; then
				continue
			fi
			if [ "$authentication" = auth2 ] && [ "$variant" != baseline ] &&
			   [ "$variant" != record-capacity ] && [ "$variant" != header-capacity ]; then
				continue
			fi
			if [ "$authentication" = ordinary ] &&
			   { [ "$variant" = record-capacity ] || [ "$variant" = header-capacity ]; }; then
				continue
			fi
			bootstrap_source="$root/src/lib/payload_mm_authvar_smm_bootstrap.c"
			executor_source="$root/src/lib/payload_mm_authvar_executor.c"
			case "$variant" in
			no-begin)
				sed '/if (delivery \&\&/ {
N
/!protected_storage/ { N; N; N; d; }
}' "$bootstrap_source" > "$temporary/$variant.c"
				;;
			no-recheck)
				sed '/if (delivery \&\&/ {
N
/PAYLOAD_MM_AUTHVAR_DELIVERY_RECHECK/ { N; N; d; }
}' "$bootstrap_source" > "$temporary/$variant.c"
				;;
			begin-order)
				sed 's/PAYLOAD_MM_AUTHVAR_DELIVERY_BEGIN,/PAYLOAD_MM_AUTHVAR_DELIVERY_RECHECK,/' \
					"$bootstrap_source" > "$temporary/$variant.c"
				;;
			recheck-order)
				sed 's/PAYLOAD_MM_AUTHVAR_DELIVERY_RECHECK,/PAYLOAD_MM_AUTHVAR_DELIVERY_BEGIN,/' \
					"$bootstrap_source" > "$temporary/$variant.c"
				;;
			record-capacity)
				sed '/^static uint64_t __maybe_unused coordinate_transaction(/,/^}/ {
s/state->policy.maximum_record_size - PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE/state->policy.maximum_record_size/
}' "$executor_source" > "$temporary/$variant.c"
				;;
			header-capacity)
				sed '/^static uint64_t __maybe_unused coordinate_transaction(/,/^}/ {
/if (state->policy.maximum_record_size <= PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE)/i\
\tstate->policy.maximum_record_size = PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE - 1;
}' "$executor_source" > "$temporary/$variant.c"
				;;
			no-request-scrub)
				sed 's/scrub(provider.service_request, sizeof(provider.service_request));/scrub(provider.service_request, 0);/' \
					"$bootstrap_source" > "$temporary/$variant.c"
				;;
			early-release)
				sed '/^enum cb_err payload_mm_authvar_service_execute(void)/,/^}/ {
/__atomic_store_n(&provider.endpoint_phase, ENDPOINT_READY,/d
/scrub(provider.service_request,/i\
\t__atomic_store_n(&provider.endpoint_phase, ENDPOINT_READY, __ATOMIC_RELEASE);
}' "$bootstrap_source" > "$temporary/$variant.c"
				;;
			early-completion)
				sed '/memcpy(mailbox, provider.service_response,/i\
\t__atomic_store_n(&mailbox->completion, PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE, __ATOMIC_RELEASE);' \
					"$bootstrap_source" > "$temporary/$variant.c"
				;;
			esac
			if [ "$variant" != baseline ]; then
				case "$variant" in
				record-capacity|header-capacity)
					cmp -s "$executor_source" "$temporary/$variant.c" && exit 1
					executor_source="$temporary/$variant.c"
					;;
				*)
					cmp -s "$bootstrap_source" "$temporary/$variant.c" && exit 1
					bootstrap_source="$temporary/$variant.c"
					;;
				esac
			fi
			for optimization in 0 2; do
				if [ "$delivery_lane" -eq 1 ] && [ "$variant" != baseline ] &&
				   [ "$optimization" -eq 0 ]; then
					continue
				fi
				set --
				if [ "$runtime_wave" -eq 1 ]; then
					set -- \
						"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_service_runtime_dispatch.c" \
						"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_protected_region.c" \
						"$root/src/soc/intel/common/block/smm/invocation_adapter.c" \
						"$root/src/soc/intel/common/block/smm/invocation_adapter_provider.c" \
						"$root/src/cpu/x86/smm/save_state_geometry.c" \
						"$root/src/soc/intel/common/block/smm/invocation_cause.c" \
						"$root/src/cpu/x86/smm_invocation_evidence.c" \
						"$root/src/cpu/x86/smm_invocation_evidence_loader.c" \
						"$root/src/cpu/x86/smm_invocation_entry.c" \
						"$root/src/cpu/x86/smm_invocation_loader_instance.c" \
						"$root/src/cpu/x86/smm_invocation_loader_composition_gate.c" \
						"$root/src/cpu/x86/smm_invocation_topology.c" \
						"$root/src/cpu/x86/smm_command.c"
				fi
				${CC:-cc} -std=gnu11 -O"$optimization" -g -Wall -Wextra -Werror \
					-Wshadow -Wstrict-prototypes -fno-builtin -fno-pie -no-pie \
					-ffunction-sections -fdata-sections -Wl,--gc-sections \
					-Wl,--wrap=memcpy \
					-fsanitize=address,undefined -fno-sanitize-recover=all \
					-fno-omit-frame-pointer -DBOOTMEM_RECEIPT_TEST -D__TEST__ -D__COREBOOT__ -D__SMM__ \
					-DMBEDTLS_CONFIG_FILE='"payload_mm_mbedtls_config.h"' \
					-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
					-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
					-include "$temporary/include/host-types.h" \
					-I"$temporary/include" -I"$root/src" -idirafter "$root/src/include" -I"$root/src/lib" \
					-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
					-I"$root/src/arch/x86/include" \
					-I"$root/src/soc/intel/common/block/include" \
					-I"$root/src/mainboard/starlabs/starbook/variants/mtl" \
					-I"$root/src/lib/payload_mm_crypto" -I"$mbedtls_source/include" \
					"$fixture_source" \
					"$root/src/lib/bootmem_reservation_receipt.c" \
					"$bootstrap_source" \
					"$root/src/lib/payload_mm_authvar.c" \
					"$root/src/lib/payload_mm_authvar_runtime.c" \
					"$root/src/lib/payload_mm_authvar_media.c" \
					"$executor_source" \
					"$root/src/lib/payload_mm_authvar_service.c" \
					"$root/src/lib/payload_mm_authvar_service_transaction.c" \
					"$root/src/lib/payload_mm_authvar_coordinator.c" \
					"$root/src/lib/payload_mm_authvar_candidate.c" \
					"$root/src/lib/payload_mm_crypto/crypto.c" \
					"$root/src/lib/payload_mm_crypto/cms.c" \
					"$root/src/lib/payload_mm_crypto/mbedtls_verify_wrap.c" \
					"$root/src/lib/payload_mm_authvar_authority_provider.c" \
					"$root/src/lib/payload_mm_authvar_authority.c" \
					"$root/src/lib/payload_mm_authvar_private_binding.c" \
					"$root/src/lib/payload_mm_authvar_private_trust.c" \
					"$root/src/lib/payload_mm_authvar_trust_store.c" \
					"$root/src/lib/payload_mm_authvar_trust_anchor.c" \
					"$root/src/lib/payload_mm_authvar_signature_db.c" \
					"$mbedtls_source/library/asn1parse.c" \
					"$mbedtls_source/library/bignum.c" \
					"$mbedtls_source/library/bignum_core.c" \
					"$mbedtls_source/library/bignum_mod.c" \
					"$mbedtls_source/library/bignum_mod_raw.c" \
					"$mbedtls_source/library/constant_time.c" \
					"$mbedtls_source/library/md.c" \
					"$mbedtls_source/library/oid.c" \
					"$mbedtls_source/library/pk.c" \
					"$mbedtls_source/library/pkparse.c" \
					"$mbedtls_source/library/rsa.c" \
					"$mbedtls_source/library/rsa_alt_helpers.c" \
					"$mbedtls_source/library/sha256.c" \
					"$mbedtls_source/library/sha512.c" \
					"$mbedtls_source/library/x509.c" \
					"$mbedtls_source/library/x509_crt.c" \
					"$mbedtls_source/library/platform_util.c" \
					"$root/src/lib/payload_mm_authvar_set_preflight.c" \
					"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
					"$root/src/lib/payload_mm_authvar_view.c" \
					"$root/src/lib/payload_mm_authvar_bundle.c" \
					"$root/src/lib/payload_mm_authvar_certdb.c" \
					"$root/src/lib/payload_mm_authvar_mode.c" \
					"$root/src/lib/payload_mm_authvar_format.c" \
					"$root/src/lib/payload_mm_authvar_route.c" \
					"$root/src/lib/payload_mm_authvar_fv.c" \
					"$root/src/lib/payload_mm_authvar_ftw.c" \
					"$root/src/lib/payload_mm_authvar_store.c" \
					"$root/src/lib/payload_mm_authvar_store_semantics.c" \
					"$root/src/lib/payload_mm_authvar_record.c" \
					"$root/src/lib/payload_mm_authvar_writer.c" \
					-I"$mbedtls_source/library" -Wl,--wrap=mbedtls_rsa_parse_pubkey \
					"$@" -pthread -o "$temporary/test"
				if [ "$delivery_lane" -eq 1 ]; then
					modes=delivery
					if [ "$private_buffer" -eq 1 ]; then
						modes='delivery delivery-begin-denied delivery-recheck-denied'
					fi
					for mode in $modes; do
						result=0
						ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
							"$temporary/test" "$mode" > "$temporary/delivery.log" 2>&1 || result=$?
						if [ "$variant" != baseline ]; then
							[ "$result" -eq 134 ]
							grep -q 'provider stack line' "$temporary/delivery.log"
							break
						fi
						if [ "$mode" = delivery ]; then
							expected_status=0
						else
							expected_status=78
						fi
						if [ "$result" -ne "$expected_status" ]; then
							cat "$temporary/delivery.log" >&2
							exit 1
						fi
						if grep -E 'provider stack line|provider reply:|runtime error:|Sanitizer' \
							"$temporary/delivery.log"; then
							exit 1
						fi
					done
					if [ "$variant" = baseline ]; then
						ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
							"$temporary/test" normal
					fi
					continue
				fi
				if [ "$runtime_wave" -eq 1 ]; then
					sh "$root/tests/mainboard/starlabs/starbook_mtl_authvar_service_runtime_wave_run.sh" \
						"$temporary/test" "$temporary" "$authentication"
					continue
				fi
				if [ "$variant" = baseline ]; then
					ASAN_OPTIONS=detect_leaks=0 "$temporary/test" capacity-edge
					set --
					if [ "$authentication" = auth2 ]; then
						set -- "$temporary/auth2.bin" "$temporary/expected-binding.bin"
					fi
					for mode in normal mailbox-drift; do
						ASAN_OPTIONS=detect_leaks=0 "$temporary/test" "$mode" "$@"
					done
					if [ "$authentication" = auth2 ]; then
						ASAN_OPTIONS=detect_leaks=0 "$temporary/test" normal "$@" wrong-content
						result=0
						ASAN_OPTIONS=detect_leaks=0 "$temporary/test" normal \
							"$temporary/auth2.bin" "$temporary/wrong-binding.bin" \
							> "$temporary/wrong-binding.log" 2>&1 || result=$?
						[ "$result" -eq 134 ]
						grep -q 'memcmp(binding.data, expected_binding' "$temporary/wrong-binding.log"
					fi
					result=0
					ASAN_OPTIONS=detect_leaks=0 "$temporary/test" proof-drift "$@" || result=$?
					[ "$result" -eq 77 ]
				else
					result=0
					set --
					if [ "$authentication" = auth2 ]; then
						set -- "$temporary/auth2.bin" "$temporary/expected-binding.bin"
					fi
					ASAN_OPTIONS=detect_leaks=0 "$temporary/test" normal "$@" \
						> "$temporary/$variant-O$optimization.log" 2>&1 || result=$?
					if [ "$variant" = no-request-scrub ]; then
						[ "$result" -eq 132 ]
					else
						[ "$result" -eq 134 ]
					fi
				fi
			done
		done
	done
done
if [ "$delivery_lane" -eq 1 ]; then
	echo 'Real op9 transaction/publication and eight-op OFF/ON regression: PASS (held binding modeled)'
else
	echo 'Real bootstrap and eight-operation provider stack: PASS (host hardware boundaries)'
fi
