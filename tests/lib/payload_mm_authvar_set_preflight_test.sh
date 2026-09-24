#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
temporary="$(mktemp -d "${TMPDIR:-/tmp}/payload-mm-authvar-set-preflight.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

compile()
{
	optimization="$1"
	preflight_source="$2"
	output="$3"
	log="$4"

	if ! ${HOSTCC:-cc} -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all -fno-builtin \
		-D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_set_preflight_test.c" \
		"$preflight_source" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" -o "$output" \
		>"$log" 2>&1; then
		cat "$log" >&2
		return 1
	fi
}

for optimization in 0 2; do
	output="$temporary/test-O$optimization"
	compile "$optimization" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" "$output" \
		"$temporary/test-O$optimization.log"
	ASAN_OPTIONS=detect_leaks=1 "$output"
done

check_mutant()
{
	name="$1"
	mutant="$2"

	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_set_preflight.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		output="$temporary/mutant-$name-O$optimization"
		log="$temporary/mutant-$name-O$optimization.log"
		if ! compile "$optimization" "$mutant" "$output" "$log"; then
			echo "ERROR: $name O$optimization mutant did not compile" >&2
			exit 1
		fi
		if ASAN_OPTIONS=detect_leaks=1 "$output" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant="$temporary/runtime-hidden.c"
sed 's/snapshot->at_runtime && existing &&/false \&\& existing \&\&/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant runtime-hidden "$mutant"

mutant="$temporary/attribute-drift.c"
sed 's/stored_attributes != existing->attributes/stored_attributes != existing->attributes \&\& false/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant attribute-drift "$mutant"

mutant="$temporary/auth2-structure.c"
sed 's/) != PAYLOAD_MM_AUTHVAR_FORMAT_OK/) == PAYLOAD_MM_AUTHVAR_FORMAT_OK/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant auth2-structure "$mutant"

mutant="$temporary/counter.c"
sed 's/request->data_size != PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE/false/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant counter-size "$mutant"

mutant="$temporary/reserved.c"
sed '0,/payload_mm_authvar_bundle_key_reserved(request->vendor_guid,/s//false \&\& payload_mm_authvar_bundle_key_reserved(request->vendor_guid,/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant reserved "$mutant"

mutant="$temporary/empty-append.c"
sed 's/if (append && !request->data_size)/if (append \&\& !request->data_size \&\& false)/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant empty-append "$mutant"

mutant="$temporary/post-auth.c"
sed '/plan->post_auth_status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;/,/return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;/ s/if (snapshot->at_runtime &&/if (false \&\& snapshot->at_runtime \&\&/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant post-auth-runtime "$mutant"

mutant="$temporary/auth2-metadata.c"
sed 's/if (!payload_mm_authvar_auth2_metadata_valid(&auth2) ||/if ((!payload_mm_authvar_auth2_metadata_valid(\&auth2) \&\& false) ||/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant auth2-metadata "$mutant"

mutant="$temporary/ordinary-runtime.c"
sed 's/if (snapshot->at_runtime && request->attributes &&/if (false \&\& snapshot->at_runtime \&\& request->attributes \&\&/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant ordinary-runtime "$mutant"

mutant="$temporary/store-alias.c"
sed 's/ranges_overlap(plan, sizeof(\*plan), index->store, index->store_size) ||/(false \&\& ranges_overlap(plan, sizeof(*plan), index->store, index->store_size)) ||/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant store-alias "$mutant"

mutant="$temporary/post-auth-not-found.c"
sed '0,/if (!(request->attributes \&/s//if (false \&\& !(request->attributes \&/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant post-auth-not-found "$mutant"

mutant="$temporary/no-access-delete.c"
sed '/if (append && !request->data_size)/,$ s/if (!(request->attributes \&/if ((request->attributes \&/' \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" > "$mutant"
check_mutant no-access-delete "$mutant"

printf '%s\n' 'Payload-MM authenticated-variable SET preflight tests: PASS'
