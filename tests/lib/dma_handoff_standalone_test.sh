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
		"$root/tests/lib/dma_handoff_standalone_test.c" \
		"$root/src/lib/dma_handoff.c" "$root/src/lib/crc_byte.c" \
		-Wl,--gc-sections \
		-o "$temporary/$name"
	"$temporary/$name"
}

build_and_run ordinary
build_and_run optimized -O2
build_and_run sanitized -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer

sed 's/if (((const struct dma_handoff_header \*)address)->generation !=/if (false \&\& ((const struct dma_handoff_header *)address)->generation !=/' \
	"$root/src/lib/dma_handoff.c" > "$temporary/dma-handoff-generation-mutant.c"
if cmp -s "$temporary/dma-handoff-generation-mutant.c" \
	"$root/src/lib/dma_handoff.c"; then
	echo 'ERROR: producer-generation mutant changed nothing' >&2
	exit 1
fi
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror \
	-ffunction-sections -fdata-sections \
	-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -fno-builtin \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" "$root/tests/lib/dma_handoff_standalone_test.c" \
	"$temporary/dma-handoff-generation-mutant.c" "$root/src/lib/crc_byte.c" \
	-Wl,--gc-sections \
	-o "$temporary/generation-mutant"
if "$temporary/generation-mutant" >/dev/null 2>&1; then
	echo 'ERROR: producer accepted a blob from a foreign PRH generation' >&2
	exit 1
fi

cc -std=gnu11 -O2 -ffunction-sections -fdata-sections \
	-D__TEST__ -D__COREBOOT__ -D__RAMSTAGE__ -fno-builtin \
	-include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
	-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
	-I"$root/src" -I"$root/src/include" -I"$root/src/commonlib/include" \
	-I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
	-I"$temporary/include" "$root/tests/lib/dma_handoff_standalone_test.c" \
	"$root/src/lib/dma_handoff.c" "$root/src/lib/crc_byte.c" \
	-Wl,--gc-sections -o "$temporary/fixture"
"$temporary/fixture" --fixture > "$temporary/coreboot-dma-handoff.bin"
test "$(wc -c < "$temporary/coreboot-dma-handoff.bin")" -eq 104
"$temporary/fixture" --q35-fixture > "$temporary/coreboot-q35-dma-handoff.bin"
test "$(wc -c < "$temporary/coreboot-q35-dma-handoff.bin")" -eq 104
if [ -n "${DMA_HANDOFF_FIXTURE_OUTPUT:-}" ]; then
	cp "$temporary/coreboot-dma-handoff.bin" "$DMA_HANDOFF_FIXTURE_OUTPUT"
fi
if [ -n "${Q35_DMA_HANDOFF_FIXTURE_OUTPUT:-}" ]; then
	cp "$temporary/coreboot-q35-dma-handoff.bin" \
		"$Q35_DMA_HANDOFF_FIXTURE_OUTPUT"
fi
printf '%s\n' \
	'DMA handoff producer ordinary/O2/ASan+UBSan/generation-mutant tests: PASS'
