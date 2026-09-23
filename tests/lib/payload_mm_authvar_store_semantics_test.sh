#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

for optimization in 0 2; do
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_store_semantics_test.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		-o "$tmp/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization"
done

check_mutant()
{
	name="$1"
	mutant="$2"
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_store_semantics.c"; then
		echo "ERROR: $name mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$name-O$optimization"
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_store_semantics_test.c" \
			"$root/src/lib/payload_mm_authvar_store.c" "$mutant" \
			-o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			echo "ERROR: $name O$optimization mutant survived" >&2
			exit 1
		fi
	done
}

mutant="$tmp/semantics-next-added.c"
sed 's/if (index->store\[entry->record_offset + 2U\] ==/if (false \&\& index->store[entry->record_offset + 2U] ==/' \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" > "$mutant"
check_mutant next-added "$mutant"

mutant="$tmp/semantics-dirty-tail.c"
sed 's/if (index->dirty_tail_offset)/if (false \&\& index->dirty_tail_offset)/' \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" > "$mutant"
check_mutant dirty-tail "$mutant"

mutant="$tmp/semantics-query-mask.c"
sed 's/if (attributes \& PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE)/if (attributes \& PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE || attributes \& ~PAYLOAD_MM_AUTHVAR_ATTR_SUPPORTED)/' \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" > "$mutant"
check_mutant query-mask "$mutant"

mutant="$tmp/semantics-query-append.c"
sed 's/if (attributes \& PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE)/if (attributes \& (PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE | PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE))/' \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" > "$mutant"
check_mutant query-append "$mutant"

awk '/payload_mm_authvar_store_semantics[.]c/ && \
     $0 !~ /CONFIG_PAYLOAD_MM_AUTHVAR_STORE_SEMANTICS/ { bad = 1 } \
     END { exit bad }' "$root/src/lib/Makefile.mk"

printf '%s\n' 'Payload-MM authenticated-variable semantics tests: PASS'
