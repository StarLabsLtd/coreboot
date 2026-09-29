#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATIONS 1' \
	'#define CONFIG_BOOTMEM_ALIGNED_RESERVATION_RECEIPT 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_RECEIPT 1' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-no-pie
	-D__COREBOOT__ -D__TEST__
	-include $root/src/include/kconfig.h -include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include"
source="$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_backing_authority_test.c"

for flags in '-O0' '-O2' \
	'-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' \
	'-O1 -g -fsanitize=thread -fno-omit-frame-pointer'; do
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags "$source" -o "$temporary/backing-test"
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
		TSAN_OPTIONS=halt_on_error=1 "$temporary/backing-test"
done

# The production ramstage object is 32-bit and must remain warning-clean and
# bounded independently of the native behavioral executable.
# shellcheck disable=SC2086
${CC:-cc} $common -m32 -O2 -fstack-usage -c \
	"$root/src/lib/payload_mm_authvar_presence_lifecycle_close_backing.c" \
	-o "$temporary/backing-ia32.o"
awk -F '\t' '$2 > 512 { exit 1 }' "$temporary/backing-ia32.su"

# Reservation is registered at BS_PRE_DEVICE exit, before device init can load
# SMM. The loader moves the verifier directly into its protected destination.
grep -q 'BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_EXIT, reserve_or_die' \
	"$root/src/lib/payload_mm_authvar_presence_lifecycle_close_publication.c"
grep -q 'payload_mm_authvar_presence_lifecycle_close_backing_verifier_take' \
	"$root/src/cpu/x86/smm/smm_module_loader.c"

echo 'payload_mm lifecycle-close backing authority tests: PASS'
