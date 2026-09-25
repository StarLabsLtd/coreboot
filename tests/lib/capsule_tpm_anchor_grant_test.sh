#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > \
	"$temporary/include/config.h"

cases='validator platform-validator platform-success success mismatch-generation mismatch-transaction
mismatch-current mismatch-candidate mismatch-null-current mismatch-alias
 mismatch-authority-overlap
install-unprotected install-mutate-grant install-mutate-binding
install-mutate-authority install-malformed'

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
		"$root/tests/lib/capsule_tpm_anchor_grant_test.c" \
		"$root/src/security/tpm/capsule_anchor_grant.c" \
		"$root/src/security/tpm/capsule_anchor.c" \
		"$root/src/security/tpm/capsule_anchor_platform.c" \
		-o "$temporary/$name"
	for case_name in $cases; do
		"$temporary/$name" "$case_name"
	done
}

build_and_run o0 -O0
build_and_run o2 -O2
build_and_run asan -O1 -fsanitize=address -fno-omit-frame-pointer
build_and_run ubsan -O1 -fsanitize=undefined -fno-omit-frame-pointer

range_build_and_run()
{
	name=$1
	source=$2
	shift 2
	range_source="$temporary/$name-range.c"
	sed "s#../../src/security/tpm/capsule_anchor_grant.c#$source#" \
		"$root/tests/lib/capsule_tpm_anchor_grant_range_test.c" > \
		"$range_source"
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
		-I"$temporary/include" "$range_source" -o "$temporary/$name-range"
	"$temporary/$name-range"
}

source_file="$root/src/security/tpm/capsule_anchor_grant.c"
range_build_and_run range-o0 "$source_file" -O0
range_build_and_run range-o2 "$source_file" -O2
range_build_and_run range-asan "$source_file" -O1 -fsanitize=address \
	-fno-omit-frame-pointer
range_build_and_run range-ubsan "$source_file" -O1 -fsanitize=undefined \
	-fno-omit-frame-pointer

range_mutant()
{
	name=$1
	old=$2
	new=$3
	mutant="$temporary/$name.c"
	sed "s#$old#$new#" "$source_file" > "$mutant"
	if cmp -s "$source_file" "$mutant"; then
		echo "ERROR: $name mutation was not applied" >&2
		exit 1
	fi
	if range_build_and_run "$name" "$mutant" -O2 >/dev/null 2>&1; then
		echo "ERROR: $name mutation survived" >&2
		exit 1
	fi
}

range_mutant range-exclusive-end \
	'left_base > UINTPTR_MAX - (left_size - 1U)' \
	'left_base > UINTPTR_MAX - left_size'
range_mutant range-null-input \
	'!left || !right || !left_size' \
	'!left_size'
range_mutant range-inclusive-contact \
	'left_base <= right_last && right_base <= left_last' \
	'left_base < right_last \&\& right_base < left_last'
