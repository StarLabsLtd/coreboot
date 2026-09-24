#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-provider.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

compile_test()
{
	provider_source=$1
	output=$2
	optimization=$3
	${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-idirafter "$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_authority_provider_test.c" \
		"$provider_source" "$root/src/lib/payload_mm_authvar_store.c" \
		-o "$output"
}

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	compile_test "$root/src/lib/payload_mm_authvar_authority_provider.c" \
		"$output" "$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

mutant_test()
{
	label=$1
	expression=$2
	mutant="$temporary/$label.c"
	sed "$expression" "$root/src/lib/payload_mm_authvar_authority_provider.c" \
		> "$mutant"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_authority_provider.c"; then
		echo "ERROR: $label mutant did not change source" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/$label-O$optimization"
		if ! compile_test "$mutant" "$output" "$optimization"; then
			echo "ERROR: $label O$optimization mutant did not compile" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$output" >/dev/null 2>&1; then
			echo "ERROR: $label O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant_test private-no-memory \
	's/case PAYLOAD_MM_VERIFY_NO_MEMORY:/case PAYLOAD_MM_VERIFY_NO_MEMORY: return status;/'
mutant_test private-tuple \
	's/!private_result_valid(request, \&draft)/!private_result_valid(request, \&draft) \&\& false/'
mutant_test private-tail \
	's/!bytes_zero(verification->new_binding + binding_size,/bytes_zero(verification->new_binding + binding_size,/'
mutant_test snapshot-seal \
	's/if (!snapshot_unchanged(request, \&snapshot))/if (!snapshot_unchanged(request, \&snapshot) \&\& false)/'
mutant_test payload-descriptor \
	's/snapshot->content\[4\].data != snapshot->new_payload.data/false/'
mutant_test volatile-private \
	'/status = private_profile(\&snapshot);/,+2 s/if (status != PAYLOAD_MM_VERIFY_OK)/if (false)/'
mutant_test private-route-target \
	's/route->target != PAYLOAD_MM_AUTHVAR_TARGET_PRIVATE/false/'
mutant_test private-route-count \
	's/route->authority_count != 1U/false/'
mutant_test private-route-tail \
	's/route->authorities\[1\] != PAYLOAD_MM_AUTHVAR_AUTHORITY_NONE/false/'
mutant_test trust-route-flags \
	's/!route->require_signature_list/false/'
mutant_test payload-cert-target \
	's/route \&\& route->target == PAYLOAD_MM_AUTHVAR_TARGET_PK/route \&\& true/'
mutant_test context-accept \
	's/if (context)/if (false \&\& context)/'
mutant_test unknown-status \
	'/default:/,+1 s/return PAYLOAD_MM_VERIFY_INTERNAL;/return PAYLOAD_MM_VERIFY_REJECTED;/'
mutant_test trust-result \
	'/if (status == PAYLOAD_MM_VERIFY_OK \&\& accepted !=/,/status = PAYLOAD_MM_VERIFY_INTERNAL;/ s/status = PAYLOAD_MM_VERIFY_INTERNAL;/status = PAYLOAD_MM_VERIFY_OK;/'
mutant_test store-index \
	's/if (!payload_mm_authvar_store_index_valid(request->index))/if (!payload_mm_authvar_store_index_valid(request->index) \&\& false)/'
mutant_test failure-output \
	'/if (!inputs_valid(request, verification, \&snapshot))/,/if (context)/ { /memset(verification, 0, sizeof(\*verification));/d; }'
