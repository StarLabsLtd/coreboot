#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-D__COREBOOT__ -D__TEST__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/lib -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include -pthread -no-pie"
sources="$root/tests/lib/payload_mm_authvar_presence_s3_record_test.c
	$root/src/lib/payload_mm_authvar_presence_s3_record.c
	$root/src/lib/payload_mm_authvar_presence.c
	$root/src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint.c"

for profile in normal o0 o2 ia32-o0 ia32-o2 asan ubsan tsan; do
	case "$profile" in
	normal) flags='' ;;
	o0) flags=-O0 ;;
	o2) flags=-O2 ;;
	ia32-o0) flags='-O0 -m32 -march=i686' ;;
	ia32-o2) flags='-O2 -m32 -march=i686' ;;
	asan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address' ;;
	ubsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=undefined' ;;
	tsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=thread -Wno-tsan' ;;
	esac
	# crc_byte.c also defines legacy crc7/crc16 helpers whose existing integer
	# promotion warnings are unrelated to this record. Keep strict conversion
	# diagnostics on every file under test and suppress only that dependency.
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags -Wno-conversion -c "$root/src/lib/crc_byte.c" \
		-o "$temporary/crc-$profile.o"
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources "$temporary/crc-$profile.o" \
		-o "$temporary/$profile"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	TSAN_OPTIONS=halt_on_error=1 \
		"$temporary/$profile"
done

# The SMM target is IA-32. All state publication atomics must remain inline
# there rather than acquiring a hidden libatomic or legacy __sync dependency.
${CC:-cc} $common -O2 -m32 -march=i686 -ffreestanding -fno-pic -c \
	"$root/src/lib/payload_mm_authvar_presence_s3_record.c" \
	-o "$temporary/s3-record-ia32.o"
if nm -u "$temporary/s3-record-ia32.o" | grep -Eq '__atomic|__sync|libatomic'; then
	echo 'ERROR: IA-32 S3 record gained an out-of-line atomic dependency' >&2
	exit 1
fi

# With the reservation disabled, the new translation unit must not enter the
# SMM source set; enabling exactly that symbol must add it.
makefile="$temporary/config-identity.mk"
{
	printf '%s\n' 'include $(ROOT)/src/lib/Makefile.mk' 'all:'
	printf '\t%s\n' \
		'@printf '\''%s\n'\'' '\''$(filter payload_mm_authvar_presence_s3_record.c,$(smm-y))'\'''
} > "$makefile"
off=$(${MAKE:-make} -s -f "$makefile" ROOT="$root" \
	CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM=)
on=$(${MAKE:-make} -s -f "$makefile" ROOT="$root" \
	CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM=y)
test -z "$off"
test "$on" = payload_mm_authvar_presence_s3_record.c

echo 'Payload-MM authenticated-variable S3 record normal/O0/O2/IA32/sanitizer/TSan tests: PASS'
