#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

build_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -fdata-sections -Wl,--gc-sections "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/tpm2_platform_auth_test.c" \
		"$root/src/security/tpm/platform_auth.c" \
		"$root/src/security/tpm/pre_os_lifecycle.c" \
		-o "$temporary/$name"
	"$temporary/$name"
}

build_and_run o0 -O0
build_and_run o2 -O2
build_and_run strict -O2 -Wshadow -Wstrict-prototypes

"${CC:-cc}" -std=gnu11 -O2 -Wall -Wextra -Werror -Wshadow -Wconversion \
	-Wstrict-prototypes -D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" -c "$root/src/security/tpm/platform_auth.c" \
	-o "$temporary/platform_auth_strict.o"
build_and_run asan -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer

if grep -Eq 'smm-.*platform_auth' "$root/src/security/tpm/Makefile.mk"; then
	echo "platform authorization codecs are linked into SMM" >&2
	exit 1
fi
