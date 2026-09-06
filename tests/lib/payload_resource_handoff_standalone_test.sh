#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
	-ffunction-sections -fdata-sections \
	-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -fno-builtin \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$root/build" \
	"$root/tests/lib/payload_resource_handoff_standalone_test.c" \
	"$root/src/lib/payload_resource_handoff.c" "$root/src/lib/crc_byte.c" \
	-Wl,--gc-sections -o "$temporary/payload-resource-handoff-test"
"$temporary/payload-resource-handoff-test"

cc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
	-ffunction-sections -fdata-sections \
	-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -fno-builtin \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$root/build" \
	"$root/tests/lib/payload_resource_handoff_q35_test.c" \
	"$root/src/mainboard/emulation/qemu-q35/payload_resource_handoff.c" \
	-Wl,--gc-sections -o "$temporary/payload-resource-handoff-q35-test"
"$temporary/payload-resource-handoff-q35-test"
printf '%s\n' 'payload resource handoff standalone tests: PASS'
