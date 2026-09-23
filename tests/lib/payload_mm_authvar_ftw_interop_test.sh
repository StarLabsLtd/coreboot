#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu
root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
fixtures="$root/tests/lib/fixtures/payload_mm_authvar_ftw_edk2_2609"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include" "$tmp/fixtures"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$tmp/include/config.h"

(cd "$fixtures" && sha256sum -c SHA256SUMS)
if [ -d "${EDK2_2609_SOURCE:-/home/sean/Documents/edk2}/.git" ]; then
	"$root/tests/lib/payload_mm_authvar_ftw_edk2_capture.sh" "$tmp/fixtures"
	for fixture in "$fixtures"/*.bin; do
		cmp "$fixture" "$tmp/fixtures/$(basename "$fixture")"
	done
fi

for optimization in 0 2; do
	mkdir -p "$tmp/coreboot-O$optimization"
	common="-std=gnu11 -O$optimization -Wall -Wextra -Werror -Wconversion -Wshadow"
	# shellcheck disable=SC2086
	cc $common -Wstrict-prototypes -fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		"$root/tests/lib/payload_mm_authvar_ftw_interop_test.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" -o "$tmp/decoder-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/decoder-O$optimization" "$fixtures"

	# shellcheck disable=SC2086
	cc $common -Wstrict-prototypes -Wno-unused-function \
		-fsanitize=address,undefined \
		-fno-sanitize-recover=all -fno-builtin -D__TEST__ -D__COREBOOT__ \
		-D__SMM__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$tmp/include" -I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$root/tests/lib" \
		"$root/tests/lib/payload_mm_authvar_ftw_coreboot_oracle_test.c" \
		"$root/src/lib/payload_mm_authvar.c" \
		"$root/src/lib/payload_mm_authvar_runtime.c" \
		"$root/src/lib/payload_mm_authvar_media.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		"$root/src/lib/payload_mm_authvar_ftw.c" \
		"$root/src/lib/payload_mm_authvar_store.c" \
		"$root/src/lib/payload_mm_authvar_store_semantics.c" \
		"$root/src/lib/payload_mm_authvar_writer.c" \
		-Wl,--wrap=payload_mm_authvar_media_fail_closed \
		-Wl,--wrap=payload_mm_authvar_media_cache_invalidate \
		-Wl,--wrap=payload_mm_authvar_media_cache_bind \
		-o "$tmp/oracle-O$optimization"
	ASAN_OPTIONS=detect_leaks=1 "$tmp/oracle-O$optimization" \
		"$tmp/coreboot-O$optimization"
	if [ -d "${EDK2_2609_SOURCE:-/home/sean/Documents/edk2}/.git" ]; then
		"$root/tests/lib/payload_mm_authvar_ftw_edk2_capture.sh" \
			"$tmp/fixtures" "$tmp/coreboot-O$optimization"
	fi
done

printf '%s\n' 'Payload-MM authenticated-variable EDK2 interoperability: PASS'
