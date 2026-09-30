#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_S3_ROUTE 1' \
	'#define CONFIG_SMM_APMC_ROUTE_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE 1' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -D__SMM__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/lib -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include -no-pie -pthread"
sources="$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_s3_route_test.c
	$root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c
	$root/src/lib/payload_mm_authvar_presence_lifecycle_close_s3_route.c"

for profile in o0 o2 asan ubsan tsan; do
	case "$profile" in
	o0) flags=-O0 ;;
	o2) flags=-O2 ;;
	asan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address' ;;
	ubsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=undefined' ;;
	tsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=thread -Wno-tsan' ;;
	esac
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/$profile"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	TSAN_OPTIONS=halt_on_error=1 "$temporary/$profile"
done

# Genuine production SMM compiles: no __TEST__ may mask production-only paths.
production="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__SMM__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include"
# shellcheck disable=SC2086
${CC:-cc} $production -O2 -c \
	"$root/src/lib/payload_mm_authvar_presence_lifecycle_close_s3_route.c" \
	-o "$temporary/s3-route.o"
# shellcheck disable=SC2086
${CC:-cc} $production -O2 -m32 -c \
	"$root/src/lib/payload_mm_authvar_presence_lifecycle_close_s3_route.c" \
	-o "$temporary/s3-route-ia32.o"

if nm -u "$temporary/s3-route.o" | grep -E \
	'presence_transaction|lifecycle_close_(provision|closed_reproof)|lifecycle.*owner'; then
	echo 'closed-S3 route references forbidden transaction/owner machinery' >&2
	exit 1
fi
if rg -n 'presence_transaction|closed_reproof\(|internal_(claim|complete)|owner_context' \
	"$root/src/include/boot/payload_mm_authvar_presence_lifecycle_close_s3_route.h" \
	"$root/src/lib/payload_mm_authvar_presence_lifecycle_close_s3_route.c"; then
	echo 'closed-S3 route source references forbidden transaction/owner machinery' >&2
	exit 1
fi

# Both implementations coexist behind the one established 0xfe registry claim.
test "$(grep -c '^#define SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLAIM(ENTRY)' \
	"$root/src/include/cpu/x86/smm_command.h")" -eq 2
! grep -q 'CLOSED_S3_CLAIM' "$root/src/include/cpu/x86/smm_command.h"
sed -n '/^config PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_S3_ROUTE$/,+4p' \
	"$root/src/lib/Kconfig" | grep -q 'default n'
grep -q '^smm-$(CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_S3_ROUTE)' \
	"$root/src/lib/Makefile.mk"
git -C "$root" diff --exit-code -- \
	src/include/boot/payload_mm_authvar_presence_lifecycle_close_route.h \
	src/lib/payload_mm_authvar_presence_lifecycle_close_route.c

"$root/tests/lib/payload_mm_authvar_presence_lifecycle_close_route_test.sh"
echo 'Payload-MM authenticated-variable presence closed-S3 route tests: PASS'
