#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd -P)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/include"
cat > "$tmp/include/config.h" <<EOF
#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0
#define CONFIG_MAX_CPUS 4
#define CONFIG_SMMSTORE 0
#define CONFIG_SMMSTORE_FULL_FLASH_ACCESS 0
#define CONFIG_SMMSTORE_BLOCK_SIZE 65536
#define CONFIG_PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_COORDINATOR 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER 1
#define CONFIG_PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_RECOVERY 0
#define CONFIG_PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION 0
EOF

for opt in 0 2; do
	"${CC:-cc}" -std=gnu11 -O"$opt" -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes -fno-builtin \
		-ffunction-sections -fdata-sections -D__TEST__ -D__COREBOOT__ \
		-D__SMM__ -include "$root/src/include/kconfig.h" \
		-include "$root/src/include/rules.h" \
		-include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
		-I"$root/src" -I"$root/src/include" -I"$root/src/lib" \
		-I"$root/src/commonlib/include" \
		-I"$root/src/commonlib/bsd/include" \
		-I"$root/src/arch/x86/include" -I"$tmp/include" \
		"$root/tests/lib/payload_mm_authvar_smm_bootstrap_arena_test.c" \
		"$root/src/lib/payload_mm_authvar_smm_bootstrap.c" \
		"$root/src/lib/payload_mm_authvar_executor.c" \
		-Wl,--gc-sections -o "$tmp/arena-$opt"
	"$tmp/arena-$opt"
done
