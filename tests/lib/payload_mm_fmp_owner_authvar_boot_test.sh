#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
cat > "$temporary/include/config.h" <<'EOF'
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR 1
#define CONFIG_PAYLOAD_MM_FMP_OWNER_AUTHVAR_BOOT 1
EOF

for optimization in 0 2; do
	"${HOSTCC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fno-builtin \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_fmp_owner_authvar_boot_test.c" \
		"$root/src/lib/payload_mm_fmp_owner_authvar_boot.c" \
		-o "$temporary/test-O$optimization"
	for test_case in retry multi near-wrap candidate current third reserved \
		reserved2 authority-both identical-success identical-error \
		identical-third identical-malformed backend-reentry mutate-current \
		mutate-candidate install-fail activate-fail init-poison proof-repeat \
		proof-missing proof-mutate-live initialize-mutate-both \
		initialize-fail-mutate-both install-fail-mutate-both \
		activate-mutate-both contract storage \
		reentry generation-zero generation-max legacy-logical-absent \
		close-survival raw-read-mutate-live raw-read-mutate-both \
		snapshot-mutate-live snapshot-mutate-both preinstall-live \
		preinstall-sealed preinstall-phase-live preinstall-phase-sealed \
		retry-corrupt-live retry-corrupt-sealed retry-callback-mutate-both; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
			"$test_case"
	done
	for field in control phase contract identity current pending; do
		for half in live sealed; do
			ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
				"corrupt-$field-$half"
		done
	done
	ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
		corrupt-phase-both
done

for optimization in 0 2; do
	"${HOSTCC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wshadow -Wstrict-prototypes -fno-builtin \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ -DREAL_OWNER \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_fmp_owner_authvar_boot_test.c" \
		"$root/src/lib/payload_mm_fmp_owner_authvar_boot.c" \
		"$root/src/lib/payload_mm_fmp_owner.c" \
		-o "$temporary/real-owner-O$optimization"
	for test_case in retry multi near-wrap candidate current third reserved \
		reserved2 authority-both identical-success identical-error \
		identical-third identical-malformed backend-reentry mutate-current \
		mutate-candidate reentry legacy-logical-absent close-survival \
		raw-read-mutate-live raw-read-mutate-both snapshot-mutate-live \
		snapshot-mutate-both preinstall-live preinstall-sealed \
		preinstall-phase-live preinstall-phase-sealed retry-corrupt-live \
		retry-corrupt-sealed retry-callback-mutate-both \
		initialize-mutate-both initialize-fail-mutate-both \
		activate-mutate-both; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/real-owner-O$optimization" \
			"$test_case"
	done
	for field in control phase contract identity current pending; do
		for half in live sealed; do
			ASAN_OPTIONS=detect_leaks=1 \
				"$temporary/real-owner-O$optimization" \
				"corrupt-$field-$half"
		done
	done
	ASAN_OPTIONS=detect_leaks=1 "$temporary/real-owner-O$optimization" \
		corrupt-phase-both
done

echo "payload-mm FMP authvar boot owner tests: PASS"
