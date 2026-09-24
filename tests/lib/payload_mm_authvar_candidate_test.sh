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
		"$root/tests/lib/payload_mm_authvar_candidate_test.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
		"$root/src/lib/payload_mm_authvar_view.c" \
		"$root/src/lib/payload_mm_authvar_mode.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		-o "$tmp/test-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/test-O$optimization"
done

for guard in source candidate certdb-exact private-key runtime-write source-proof \
		enable-source-helper enable-candidate-helper; do
	mutant="$tmp/candidate-$guard.c"
	if [ "$guard" = source ]; then
		sed 's/(source_pk && !source_enable) ||/false ||/' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	elif [ "$guard" = candidate ]; then
		sed 's/(candidate_pk && !candidate_enable))/false)/' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	elif [ "$guard" = certdb-exact ]; then
		sed 's/!memcmp(workspace, certdb->data, expected_size)/!memcmp(workspace, certdb->data, 0U)/' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	elif [ "$guard" = private-key ]; then
		sed 's/if (!private_key(target->vendor_guid, target->name, target->name_size))/if (false \&\& !private_key(target->vendor_guid, target->name, target->name_size))/' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	elif [ "$guard" = runtime-write ]; then
		sed 's/if (at_runtime \&\& bundle->mutations/if (false \&\& at_runtime \&\& bundle->mutations/' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	elif [ "$guard" = source-proof ]; then
		sed 's/if (source_target \&\&/if (source_target \&\& false \&\&/' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	elif [ "$guard" = enable-source-helper ]; then
		sed 's/source_enable \&\& !payload_mm_authvar_mode_enabled/false \&\& !payload_mm_authvar_mode_enabled/g' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	else
		sed '0,/candidate_enable \&\& !payload_mm_authvar_mode_enabled/s//false \&\& !payload_mm_authvar_mode_enabled/' \
			"$root/src/lib/payload_mm_authvar_candidate.c" > "$mutant"
	fi
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_candidate.c"; then
		echo "ERROR: candidate $guard mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$guard-O$optimization"
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" \
			-I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" \
			-I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_candidate_test.c" "$mutant" \
			"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_certdb.c" \
			"$root/src/lib/payload_mm_authvar_view.c" \
			"$root/src/lib/payload_mm_authvar_mode.c" \
			"$root/src/lib/payload_mm_authvar_record.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_format.c" -o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			echo "ERROR: candidate $guard mutant survived" >&2
			exit 1
		fi
	done
done

for guard in mode-truth mode-range; do
	mutant="$tmp/mode-$guard.c"
	if [ "$guard" = mode-truth ]; then
		sed 's/\*enabled = value == 1U;/\*enabled = value != 0U;/' \
			"$root/src/lib/payload_mm_authvar_mode.c" > "$mutant"
	else
		sed 's/(key != PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE \&\&/(key == PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE \&\&/' \
			"$root/src/lib/payload_mm_authvar_mode.c" > "$mutant"
	fi
	if cmp -s "$mutant" "$root/src/lib/payload_mm_authvar_mode.c"; then
		echo "ERROR: $guard mutant changed nothing" >&2
		exit 1
	fi
	for optimization in 0 2; do
		binary="$tmp/mutant-$guard-O$optimization"
		cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
			-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
			-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
			-include "$root/src/include/kconfig.h" \
			-include "$root/src/include/rules.h" \
			-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
			-I"$tmp/include" -I"$root/src" -I"$root/src/lib" \
			-I"$root/src/include" -I"$root/src/commonlib/include" \
			-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
			"$root/tests/lib/payload_mm_authvar_candidate_test.c" \
			"$root/src/lib/payload_mm_authvar_candidate.c" \
			"$root/src/lib/payload_mm_authvar_bundle.c" \
			"$root/src/lib/payload_mm_authvar_certdb.c" \
			"$root/src/lib/payload_mm_authvar_view.c" "$mutant" \
			"$root/src/lib/payload_mm_authvar_record.c" \
			"$root/src/lib/payload_mm_authvar_store.c" \
			"$root/src/lib/payload_mm_authvar_store_semantics.c" \
			"$root/src/lib/payload_mm_authvar_format.c" -o "$binary"
		if ASAN_OPTIONS=detect_leaks=1 "$binary" >/dev/null 2>&1; then
			echo "ERROR: $guard O$optimization survived" >&2
			exit 1
		fi
	done
done

printf '%s\n' 'Payload-MM authenticated-variable candidate tests: PASS'
