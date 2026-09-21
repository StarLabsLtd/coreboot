#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_TPM2 1' \
	'#define CONFIG_MEMORY_MAPPED_TPM 1' \
	'#define CONFIG_TPM_TIS_BASE_ADDRESS 0xfed40000' \
	'#define CONFIG_TPM2_FIFO_PRE_OS_LIFECYCLE 1' > \
	"$temporary/include/config.h"

scenarios='normal wrong-family wrong-route null-route null-take null-lifecycle
begin-failure owned-handoff
begin-resurrection transmit-failure transmit-resurrection quiesce-failure
transmit-lost-locality transmit-seized-locality
quiesce-resurrection release-failure release-resurrection'

tis_scenarios='idle data-available expect lost-locality seized-locality
ready-timeout ready-bad-postcondition invalid-status
transmit-lost-locality transmit-seized-locality
release-success release-wait-timeout release-still-active release-invalid
release-seized release-lost-locality release-seized-locality'

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
		-I"$root/3rdparty/vboot/firmware/include" \
		-I"$root/3rdparty/vboot/firmware/2lib/include" \
		-I"$temporary/include" \
		"$root/tests/lib/tpm2_fifo_pre_os_lifecycle_test.c" \
		"$root/src/security/tpm/pre_os_lifecycle.c" \
		"$root/src/security/tpm/fifo_pre_os_lifecycle.c" \
		"$root/src/security/tpm/tss/tss.c" \
		-o "$temporary/$name"
	for scenario in $scenarios; do
		"$temporary/$name" "$scenario"
	done
}

build_and_run_tis()
{
	name=$1
	shift
	"${CC:-cc}" -std=gnu11 -g -Wall -Wextra -Werror -fno-builtin \
		-Wno-unused-parameter -Wno-missing-field-initializers \
		-Wno-sign-compare \
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
		-I"$temporary/include" \
		"$root/tests/lib/tpm2_fifo_tis_lifecycle_test.c" \
		"$root/src/drivers/pc80/tpm/tis.c" \
		-o "$temporary/$name-tis"
	for scenario in $tis_scenarios; do
		"$temporary/$name-tis" "$scenario"
	done
}

build_and_run o0 -O0
build_and_run o2 -O2
ASAN_OPTIONS=detect_leaks=1 build_and_run asan -O1 \
	-fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer \
	-fno-sanitize-recover=all

build_and_run_tis tis-o0 -O0
build_and_run_tis tis-o2 -O2
ASAN_OPTIONS=detect_leaks=1 build_and_run_tis tis-asan -O1 \
	-fsanitize=address -fno-omit-frame-pointer
build_and_run_tis tis-ubsan -O1 -fsanitize=undefined \
	-fno-omit-frame-pointer -fno-sanitize-recover=all

mkdir -p "$temporary/include-off"
printf '%s\n' \
	'#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
	'#define CONFIG_TPM2 1' \
	'#define CONFIG_MEMORY_MAPPED_TPM 1' \
	'#define CONFIG_TPM2_FIFO_PRE_OS_LIFECYCLE 0' > \
	"$temporary/include-off/config.h"
"${CC:-cc}" -E -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	-I"$root/3rdparty/vboot/firmware/include" \
	-I"$root/3rdparty/vboot/firmware/2lib/include" \
	-I"$temporary/include-off" \
	"$root/src/security/tpm/tss/tss.c" > "$temporary/tss-off.i"
"${CC:-cc}" -E -D__COREBOOT__ -D__RAMSTAGE__ \
	-include "$root/src/include/kconfig.h" \
	-include "$root/src/include/rules.h" \
	-I"$root/src" -I"$root/src/include" \
	-I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" \
	-I"$root/src/arch/x86/include" \
	-I"$root/3rdparty/vboot/firmware/include" \
	-I"$root/3rdparty/vboot/firmware/2lib/include" \
	-I"$temporary/include-off" \
	"$root/src/drivers/pc80/tpm/tis.c" > "$temporary/tis-off.i"
if grep -Eq 'tlcl_take_tpm2_fifo_route|tlcl_tis_route_is_taken|tis_route_state' \
	"$temporary/tss-off.i"; then
	echo "unselected TSS still contains FIFO lifecycle ownership" >&2
	exit 1
fi
driver_pattern='pc80_tis_is_fifo_route|lifecycle_owned|lifecycle_read_idle|'
driver_pattern="${driver_pattern}pc80_tis_fifo_(quiesce|validate_idle|release_locality)"
if grep -Eq "$driver_pattern" "$temporary/tis-off.i"; then
	echo "unselected FIFO driver still contains lifecycle ownership" >&2
	exit 1
fi

if grep -Eq 'smm-.*fifo_pre_os_lifecycle' \
	"$root/src/security/tpm/Makefile.mk"; then
	echo "FIFO pre-OS lifecycle owner is linked into SMM" >&2
	exit 1
fi

if grep -Eq 'CAPSULE_TPM_ANCHOR_POLICY_PROVIDER|CAPSULE_BROKER|SMI' \
	"$root/src/security/tpm/fifo_pre_os_lifecycle.c" \
	"$root/src/security/tpm/fifo_pre_os_lifecycle.h"; then
	echo "FIFO pre-OS lifecycle owner crossed its transport-only scope" >&2
	exit 1
fi
