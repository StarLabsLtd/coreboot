#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_MAX_CPUS 64' \
	'#define CONFIG_SMM_INVOCATION_RUNTIME_BINDING 1' \
	'#define CONFIG_PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_IDLE_SNAPSHOT 1' \
	'#define CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM 1' \
	'#define CONFIG_SMM_AUTHVAR_S3_STATE_SMRAM_SIZE 4096' \
	> "$temporary/include/config.h"

common="-std=gnu11 -Wall -Wextra -Werror -Wconversion -Wshadow -fno-builtin
	-Wno-unused-parameter -Wno-sign-conversion -Wno-conversion
	-D__COREBOOT__ -D__TEST__ -D__SMM__ -include $root/src/include/kconfig.h
	-include $root/src/include/rules.h
	-include $root/src/commonlib/bsd/include/commonlib/bsd/compiler.h
	-I$temporary/include -I$root/src -I$root/src/lib -I$root/src/include
	-I$root/src/commonlib/include -I$root/src/commonlib/bsd/include
	-I$root/src/arch/x86/include -I$root/src/soc/intel/common/block/include
	-I$root/src/mainboard/starlabs/starbook/variants/mtl -pthread -no-pie"
sources="$root/tests/lib/starbook_mtl_authvar_presence_s3_cold_test.c
	$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_cold.c"

for profile in o0 o2 ia32-o0 ia32-o2 asan ubsan tsan; do
	case "$profile" in
	o0) flags=-O0 ;;
	o2) flags=-O2 ;;
	ia32-o0) flags='-O0 -m32 -march=i686' ;;
	ia32-o2) flags='-O2 -m32 -march=i686' ;;
	asan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address' ;;
	ubsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=undefined' ;;
	tsan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=thread -Wno-tsan' ;;
	esac
	# shellcheck disable=SC2086
	${CC:-cc} $common $flags $sources -o "$temporary/$profile"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
	TSAN_OPTIONS=halt_on_error=1 "$temporary/$profile"
done

# Compile the actual SMM translation unit without test-only interfaces.
${CC:-cc} $common -U__TEST__ -O2 -m32 -march=i686 \
	-ffreestanding -fno-pic -c \
	"$root/src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_s3_cold.c" \
	-o "$temporary/cold-s3-ia32.o"
if nm -u "$temporary/cold-s3-ia32.o" | grep -Eq '__atomic|__sync|libatomic'; then
	echo 'ERROR: IA-32 cold S3 owner gained an out-of-line atomic dependency' >&2
	exit 1
fi

echo 'StarBook MTL authenticated-variable cold S3 owner O0/O2/IA32/sanitizer/TSan tests: PASS'
