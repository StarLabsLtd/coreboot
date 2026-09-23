#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

compile()
{
	optimization="$1"
	source="$2"
	output="$3"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_format_test.c" "$source" \
		-o "$output"
}

run_test()
{
	ASAN_OPTIONS=detect_leaks=1 "$1"
}

for optimization in 0 2; do
	binary="$tmp/test-O$optimization"
	compile "$optimization" "$root/src/lib/payload_mm_authvar_format.c" "$binary"
	run_test "$binary"
done

mutate()
{
	name="$1"
	expression="$2"
	mutant="$tmp/$name.c"

	sed "$expression" "$root/src/lib/payload_mm_authvar_format.c" > "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_format.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/$name-O$optimization"
		compile "$optimization" "$mutant" "$binary"
		if run_test "$binary" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutate auth2-fixed-bound \
	's/if (!bytes || size < AUTH2_PKCS7_OFFSET)/if (!bytes || (false \&\& size < AUTH2_PKCS7_OFFSET))/'
mutate auth2-cert-lower \
	's/certificate_size < AUTH2_CERTIFICATE_HEADER_SIZE/(false \&\& certificate_size < AUTH2_CERTIFICATE_HEADER_SIZE)/'
mutate auth2-cert-upper \
	's/certificate_size > size - AUTH2_CERTIFICATE_OFFSET/(false \&\& certificate_size > size - AUTH2_CERTIFICATE_OFFSET)/'
mutate auth2-output-zero \
	'/payload_mm_authvar_auth2_parse/,/return PAYLOAD_MM_AUTHVAR_FORMAT_OK/ s/memset(view, 0, sizeof(\*view));/\/\* mutant: retained output \*\//'
mutate timestamp-reserved \
	's/view \&\& timestamp_reserved_fields_valid(view->timestamp)/view \&\& (timestamp_reserved_fields_valid(view->timestamp) || true)/'
mutate timestamp-store-calendar \
	's/return timestamp \&\& timestamp_valid(timestamp);/return timestamp \&\& (timestamp_valid(timestamp) || true);/'
mutate certificate-type \
	's/view->certificate_type == WIN_CERT_TYPE_EFI_GUID/(view->certificate_type == WIN_CERT_TYPE_EFI_GUID || true)/'
mutate certificate-guid \
	's/!memcmp(view->certificate_guid, pkcs7_guid, sizeof(pkcs7_guid))/(!memcmp(view->certificate_guid, pkcs7_guid, sizeof(pkcs7_guid)) || true)/'
mutate list-fixed-bound \
	's/if (cursor->remaining < SIGNATURE_LIST_HEADER_SIZE)/if (false \&\& cursor->remaining < SIGNATURE_LIST_HEADER_SIZE)/'
mutate list-size-lower \
	's/list_size < SIGNATURE_LIST_HEADER_SIZE/(false \&\& list_size < SIGNATURE_LIST_HEADER_SIZE)/'
mutate list-size-upper \
	's/list_size > cursor->remaining/(false \&\& list_size > cursor->remaining)/'
mutate list-header-bound \
	's/header_size > list_size - SIGNATURE_LIST_HEADER_SIZE/(false \&\& header_size > list_size - SIGNATURE_LIST_HEADER_SIZE)/'
mutate list-stride-bound \
	'/payload_mm_authvar_signature_list_next/,/return PAYLOAD_MM_AUTHVAR_FORMAT_OK/ s/signature_size < SIGNATURE_OWNER_SIZE/(false \&\& signature_size < SIGNATURE_OWNER_SIZE)/'
mutate list-modulo \
	's/if (payload_size % signature_size)/if (false \&\& payload_size % signature_size)/'
mutate list-progress \
	's/cursor->next += list_size;/\/\* mutant: cursor did not advance \*\//'
mutate signature-output-zero \
	'/payload_mm_authvar_signature_at/,/^}/ s/memset(signature, 0, sizeof(\*signature));/\/\* mutant: retained output \*\//'
mutate signature-access-bound \
	's/list->signature_size > list->signatures.size - offset/(false \&\& list->signature_size > list->signatures.size - offset)/'

printf '%s\n' 'Payload-MM authenticated-variable format tests: PASS'
