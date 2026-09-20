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
		-I"$root/src/arch/x86/include" \
		-I"$temporary/include" \
		"$root/tests/lib/tpm_transport_release_test.c" \
		"$root/src/security/tpm/transport_release.c" \
		-o "$temporary/$name"
	"$temporary/$name"
}

build_and_run o0 -O0
build_and_run o2 -O2
build_and_run asan-ubsan -O1 -fsanitize=address,undefined \
	-fno-omit-frame-pointer -fno-sanitize-recover=all

if command -v clang >/dev/null 2>&1; then
	clang --analyze -std=gnu11 -Wall -Wextra -Werror \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$temporary/include" \
		"$root/src/security/tpm/transport_release.c"
else
	"${CC:-cc}" -fanalyzer -std=gnu11 -Wall -Wextra -Werror \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$temporary/include" -c \
		"$root/src/security/tpm/transport_release.c" \
		-o "$temporary/analyzer.o"
fi

pattern='(device/mmio|arch/io|delay\.h|timer\.h|tlcl_lib_init|smi_handler_register)'
if grep -Eq "$pattern" \
	"$root/src/security/tpm/transport_release.c"; then
	echo "transport release primitive contains a hardware or SMI dependency" >&2
	exit 1
fi

if grep -Eq 'smm-.*transport_release' \
	"$root/src/security/tpm/Makefile.mk"; then
	echo "transport release primitive is linked into SMM" >&2
	exit 1
fi
