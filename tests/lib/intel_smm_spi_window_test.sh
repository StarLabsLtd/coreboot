#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary/include/cpu/intel" "$temporary/include/cpu/x86" \
	"$temporary/include/device" "$temporary/include/intelblocks" \
	"$temporary/include/security/lockdown"

cat > "$temporary/include/cpu/intel/msr.h" <<'EOF'
#define MSR_SPCL_CHIPSET_USAGE 0x1fe
EOF
cat > "$temporary/include/cpu/x86/msr.h" <<'EOF'
#include <stdint.h>
typedef struct { uint32_t lo, hi; } msr_t;
void test_wrmsr(uint32_t index, msr_t value);
#define wrmsr test_wrmsr
EOF
cat > "$temporary/include/device/mmio.h" <<'EOF'
#include <stdint.h>
uint32_t test_read32p(uintptr_t address);
#define read32p test_read32p
EOF
cat > "$temporary/include/delay.h" <<'EOF'
void test_udelay(unsigned int usec);
#define udelay test_udelay
EOF
cat > "$temporary/include/intelblocks/fast_spi.h" <<'EOF'
#include <stdbool.h>
#include <stdint.h>
bool fast_spi_clear_sync_smi_status(void);
uint16_t fast_spi_bios_control(void);
void fast_spi_enable_wp(void);
void fast_spi_disable_wp(void);
EOF
cat > "$temporary/include/security/lockdown/lockdown.h" <<'EOF'
#include <stdbool.h>
bool enable_smm_bios_protection(void);
EOF

for optimization in 0 2; do
	binary="$temporary/window-O$optimization"
	"${CC:-cc}" -std=gnu11 -O"$optimization" -g -Wall -Wextra -Werror \
		-Wconversion -Wshadow -Wstrict-prototypes -fno-builtin \
		-fsanitize=address,undefined -fno-sanitize-recover=all \
		-I"$temporary/include" \
		-I"$root/src/soc/intel/common/block/include" \
		"$root/tests/lib/intel_smm_spi_window_test.c" -o "$binary"
	for mode in success idle-proof reentry token-mutation context-mutation \
		policy-false-to-true policy-true-to-false \
		option-false-to-true option-true-to-false \
		active-restore beginning-restore restoring-begin \
		restore-failure-poison legacy-run-success legacy-run-failure \
		option-run-success option-run-failure policy-off legacy-policy-off \
		legacy-policy-off-writable drop-wpd drop-lock \
		wrong-owner copied-token invalid-input bad-lock open-readback \
		close-readback sync-stuck open-insmm-stuck close-drain-stuck \
		close-insmm-stuck; do
		ASAN_OPTIONS=detect_leaks=1 "$binary" "$mode"
	done
done

printf '%s\n' 'Intel SMM SPI owner window tests: PASS'
