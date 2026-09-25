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
EOF

for optimization in 0 2; do
	"${HOSTCC:-cc}" -std=gnu11 -O"$optimization" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes -fno-builtin \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-D__TEST__ -D__COREBOOT__ -D__SMM__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
		-I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_fmp_owner_authvar_identity_test.c" \
		"$root/src/lib/payload_mm_fmp_owner_authvar.c" \
		"$root/src/lib/payload_mm_authvar_set_preflight.c" \
		"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_route.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		-o "$temporary/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization"
	for test_case in zero-instance uninstalled failed-install corrupt-live \
		corrupt-seal corrupt-control; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" "$test_case"
	done
	for mutation in zero-guid wrong-guid embedded-nul odd-size bad-suffix \
		trailing wrong-lsv wrong-instance duplicate; do
		ASAN_OPTIONS=detect_leaks=1 "$temporary/test-O$optimization" \
			"invalid-$mutation"
	done
done

mutant="$temporary/owner-wrong-protected-size.c"
awk '
	/index\(context, &authvar_owner, sizeof\(authvar_owner\)\)/ { }
	/!storage_is_protected\(context, &authvar_owner, sizeof\(authvar_owner\)\)/ {
		sub(/sizeof\(authvar_owner\)/, "sizeof(authvar_owner) - 1U")
		changed++
	}
	{ print }
	END { if (changed != 1) exit 2 }
' "$root/src/lib/payload_mm_fmp_owner_authvar.c" > "$mutant"
"${HOSTCC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -Wconversion \
	-Wshadow -Wstrict-prototypes -fno-builtin -fsanitize=address,undefined \
	-fno-sanitize-recover=all -D__TEST__ -D__COREBOOT__ -D__SMM__ \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$temporary/include" -I"$root/src" -I"$root/src/lib" \
	-I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	"$root/tests/lib/payload_mm_fmp_owner_authvar_identity_test.c" "$mutant" \
	"$root/src/lib/payload_mm_authvar_set_preflight.c" \
	"$root/src/lib/payload_mm_authvar_controlled_mode.c" \
	"$root/src/lib/payload_mm_authvar_bundle.c" \
	"$root/src/lib/payload_mm_authvar_certdb.c" \
	"$root/src/lib/payload_mm_authvar_mode.c" \
	"$root/src/lib/payload_mm_authvar_view.c" \
	"$root/src/lib/payload_mm_authvar_route.c" \
	"$root/src/lib/payload_mm_authvar_format.c" \
	"$root/src/lib/payload_mm_authvar_store.c" \
	"$root/src/lib/payload_mm_authvar_store_semantics.c" \
	"$root/src/lib/payload_mm_authvar_record.c" -o "$temporary/wrong-size"
if ASAN_OPTIONS=detect_leaks=1 "$temporary/wrong-size" >/dev/null 2>&1; then
	echo 'ERROR: wrong protected authority size mutant survived' >&2
	exit 1
fi

if rg -q 'payload_mm_fmp_owner_authvar_reservation' \
	"$root/src/lib/payload_mm_authvar_executor.c"; then
	echo 'FMP reservation leaked into read transaction' >&2
	exit 1
fi
printf '%s\n' 'Payload-MM FMP sealed identity/reservation O0/O2 ASan+UBSan: PASS'
