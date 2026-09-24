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
		"$root/tests/lib/payload_mm_authvar_store_test.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		-o "$tmp/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization"
done

zero_time_mutant="$tmp/payload_mm_authvar_store-zero-time.c"
sed 's/if (bytes_are(timestamp, 16, 0))/if (false \&\& bytes_are(timestamp, 16, 0))/' \
	"$root/src/lib/payload_mm_authvar_store.c" > "$zero_time_mutant"
if cmp -s "$zero_time_mutant" "$root/src/lib/payload_mm_authvar_store.c"; then
	echo 'ERROR: zero-time mutant changed nothing' >&2
	exit 1
fi
for optimization in 0 2; do
	binary="$tmp/mutant-zero-time-O$optimization"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_store_test.c" "$zero_time_mutant" \
		-o "$binary"
	if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
		echo "ERROR: zero-time O$optimization mutant survived" >&2
		exit 1
	fi
done

find_one_mutant="$tmp/payload_mm_authvar_store-find-one-duplicate.c"
sed 's/(predecessor &&/(false \&\& predecessor \&\&/' \
	"$root/src/lib/payload_mm_authvar_store.c" > "$find_one_mutant"
if cmp -s "$find_one_mutant" "$root/src/lib/payload_mm_authvar_store.c"; then
	echo 'ERROR: find-one duplicate mutant changed nothing' >&2
	exit 1
fi
for optimization in 0 2; do
	binary="$tmp/mutant-find-one-duplicate-O$optimization"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_store_test.c" "$find_one_mutant" \
		-o "$binary"
	if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
		echo "ERROR: find-one duplicate O$optimization mutant survived" >&2
		exit 1
	fi
done

winner_mutant="$tmp/payload_mm_authvar_store-find-one-winner.c"
sed '/enum cb_err payload_mm_authvar_store_find_one/,/^}/s/copy_record(entry, bytes, \&record);/if (!*found) copy_record(entry, bytes, \&record);/' \
	"$root/src/lib/payload_mm_authvar_store.c" > "$winner_mutant"
if cmp -s "$winner_mutant" "$root/src/lib/payload_mm_authvar_store.c"; then
	echo 'ERROR: find-one winner mutant changed nothing' >&2
	exit 1
fi
for optimization in 0 2; do
	binary="$tmp/mutant-find-one-winner-O$optimization"
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_store_test.c" "$winner_mutant" \
		-o "$binary"
	if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
		echo "ERROR: find-one winner O$optimization mutant survived" >&2
		exit 1
	fi
done

mutant="$tmp/payload_mm_authvar_store-erased-tail.c"
sed 's/if (record->state == PAYLOAD_MM_AUTHVAR_STATE_ERASED)/if (false \&\& record->state == PAYLOAD_MM_AUTHVAR_STATE_ERASED)/' \
	"$root/src/lib/payload_mm_authvar_store.c" > "$mutant"
if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_store.c"; then
	echo 'ERROR: erased-tail mutant changed nothing' >&2
	exit 1
fi
for optimization in 0 2; do
	binary="$tmp/mutant-erased-tail-O$optimization"
	log="$tmp/mutant-erased-tail-O$optimization.log"
	if ! cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_store_test.c" "$mutant" \
		-o "$binary" >"$log" 2>&1; then
		echo "ERROR: erased-tail O$optimization mutant did not compile" >&2
		cat "$log" >&2
		exit 1
	fi
	if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
		echo "ERROR: erased-tail O$optimization mutant survived" >&2
		exit 1
	fi
done

awk '/payload_mm_authvar_store[.]c/ && $0 !~ /CONFIG_PAYLOAD_MM_AUTHVAR_STORE_SCANNER/ { bad = 1 } END { exit bad }' \
	"$root/src/lib/Makefile.mk"

printf '%s\n' 'Payload-MM authenticated-variable store scanner tests: PASS'
