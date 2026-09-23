#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"
mkdir -p "$tmp/candidate/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1' \
	> "$tmp/candidate/include/config.h"

for optimization in 0 2; do
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-D__SMM__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_acceptance_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_media.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		-Wl,--wrap=payload_mm_authvar_media_fail_closed \
		-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
		-Wl,--wrap=payload_mm_authvar_media_cache_bind \
		-o "$tmp/acceptance-O$optimization"
	if [ "${ACCEPTANCE_FAULT_ONLY:-0}" = 1 ]; then
		ASAN_OPTIONS=detect_leaks=1 "$tmp/acceptance-O$optimization" fault-only
	else
		ASAN_OPTIONS=detect_leaks=1 "$tmp/acceptance-O$optimization"
	fi
	cc -std=gnu11 -O"$optimization" -Wall -Wextra -Werror -Wconversion \
		-Wshadow -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-D__SMM__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/candidate/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_executor_acceptance_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_media.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_record.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		"$root/src/lib/payload_mm_authvar_candidate.c" \
		"$root/src/lib/payload_mm_authvar_bundle.c" \
		"$root/src/lib/payload_mm_authvar_format.c" \
		-Wl,--wrap=payload_mm_authvar_media_fail_closed \
		-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
		-Wl,--wrap=payload_mm_authvar_media_cache_bind \
		-o "$tmp/acceptance-candidate-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 \
		"$tmp/acceptance-candidate-O$optimization" golden-only
done

printf '%s\n' 'Payload-MM authenticated-variable executor integrated acceptance: PASS'
