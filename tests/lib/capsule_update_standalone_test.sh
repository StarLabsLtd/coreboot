#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include"
printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' > "$temporary/include/config.h"

build_and_run() {
	name=$1
	shift
	cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
		-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -fno-builtin \
		-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
		-I"$temporary/include" "$@" \
		"$root/tests/lib/capsule_update_standalone_test.c" \
		"$root/src/lib/capsule_update.c" \
		"$root/src/lib/payload_mm_fmp_owner_layout.c" \
		"$root/src/lib/capsule_update_backend.c" -Wl,--gc-sections \
		-o "$temporary/$name"
	"$temporary/$name"
}

build_and_run ordinary
build_and_run optimized -O2
build_and_run sanitized -O1 -g -fsanitize=address,undefined \
	-fno-omit-frame-pointer

"$temporary/optimized" --fixture > "$temporary/coreboot-capsule-handoff.bin"
test "$(wc -c < "$temporary/coreboot-capsule-handoff.bin")" -eq 144
if [ -n "${CAPSULE_HANDOFF_FIXTURE_OUTPUT:-}" ]; then
	cp "$temporary/coreboot-capsule-handoff.bin" \
		"$CAPSULE_HANDOFF_FIXTURE_OUTPUT"
fi
if [ -n "${CDK2_CAPSULE_HANDOFF_FIXTURE:-}" ]; then
	cmp "$temporary/coreboot-capsule-handoff.bin" \
		"$CDK2_CAPSULE_HANDOFF_FIXTURE"
fi
if [ -n "${CDK2_ROOT:-}" ]; then
	cc -std=gnu11 -Wall -Wextra -Werror -I"$CDK2_ROOT/include" \
		"$root/tests/lib/capsule_update_cdk2_fixture.c" \
		-o "$temporary/cdk2-fixture"
	"$temporary/cdk2-fixture" > "$temporary/cdk2-capsule-handoff.bin"
	cmp "$temporary/coreboot-capsule-handoff.bin" \
		"$temporary/cdk2-capsule-handoff.bin"
	cc -std=gnu11 -Wall -Wextra -Werror -ffunction-sections \
		-fdata-sections -I"$CDK2_ROOT/include" -I"$CDK2_ROOT/src/boot" \
		"$root/tests/lib/capsule_update_cdk2_parser_test.c" \
		"$CDK2_ROOT/src/modules/system_fmp/handoff.c" \
		-Wl,--gc-sections -o "$temporary/cdk2-parser"
	"$temporary/cdk2-parser" "$temporary/coreboot-capsule-handoff.bin"
fi
printf '%s\n' \
	'Capsule contract ordinary/O2/ASan+UBSan/ABI fixture tests: PASS'
