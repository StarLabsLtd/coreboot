#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_CAPSULE_TPM_ANCHOR_TRANSITION 1' > \
	"$temporary/include/config.h"

build_and_run()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin "$@" \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ \
		-include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$temporary/include" \
		"$root/tests/lib/payload_mm_fmp_owner_prepared_reader_test.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		"$root/src/lib/payload_mm_fmp_owner_journal_format.c" \
		"$root/src/lib/payload_mm_fmp_owner_prepared_reader.c" \
		-o "$temporary/$name"
	"$temporary/$name"
}

build_and_run prepared-reader-o0 -O0
build_and_run prepared-reader-o2 -O2
build_and_run prepared-reader-strict -O2 -Wshadow -Wconversion \
	-Wstrict-prototypes
build_and_run prepared-reader-asan -O1 -fsanitize=address \
	-fno-omit-frame-pointer
build_and_run prepared-reader-ubsan -O1 -fsanitize=undefined \
	-fno-omit-frame-pointer

# The reader uses the capsule layout validator. Kconfig must not admit a
# transition build without the contract which supplies that ramstage object.
mkdir -p "$temporary/config-invalid" "$temporary/build-invalid"
cp "$root/configs/config.emulation_qemu_x86_q35_smm_tseg" \
	"$temporary/config-invalid/.config"
printf '%s\n' \
	'CONFIG_TPM2=y' \
	'CONFIG_PAYLOAD_MM_AUTHVAR_CONTRACT=y' \
	'CONFIG_CAPSULE_TPM_ANCHOR_GRANT=y' \
	'CONFIG_CAPSULE_UPDATE_CONTRACT=n' \
	'CONFIG_CAPSULE_TPM_ANCHOR_TRANSITION=y' >> \
	"$temporary/config-invalid/.config"
make -C "$root" obj="$temporary/build-invalid" \
	DOTCONFIG="$temporary/config-invalid/.config" olddefconfig >/dev/null
! grep -qx 'CONFIG_CAPSULE_UPDATE_CONTRACT=y' \
	"$temporary/config-invalid/.config"
! grep -qx 'CONFIG_CAPSULE_TPM_ANCHOR_TRANSITION=y' \
	"$temporary/config-invalid/.config"
