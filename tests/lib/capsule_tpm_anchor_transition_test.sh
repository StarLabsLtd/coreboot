#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
mkdir -p "$tmp/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$tmp/include/config.h"

build_and_run()
{
	name=$1
	shift
	cc -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-ffunction-sections -fdata-sections -Wl,--gc-sections "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" \
		-I"$root/3rdparty/vboot/firmware/include" \
		-I"$root/3rdparty/vboot/firmware/2lib/include" \
		-I"$tmp/include" \
		"$root/src/security/tpm/pre_os_lifecycle.c" \
		"$root/src/security/tpm/capsule_anchor_grant.c" \
		"$root/src/security/tpm/capsule_anchor_transition.c" \
		"$root/tests/lib/capsule_tpm_anchor_transition_test.c" \
		"$root/src/security/tpm/capsule_anchor_authorization.c" \
		-o "$tmp/test-$name"
	"$tmp/test-$name"
}

build_and_run o0 -O0
build_and_run o2 -O2
ASAN_OPTIONS=detect_leaks=1 build_and_run asan -O1 \
	-fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer

if grep -Eq 'smm-.*capsule_anchor_transition' \
	"$root/src/security/tpm/Makefile.mk"; then
	echo "capsule TPM transition coordinator is linked into SMM" >&2
	exit 1
fi
