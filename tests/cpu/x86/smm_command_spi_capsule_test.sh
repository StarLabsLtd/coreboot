#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
gate=${1:?retained gate directory required}
mkdir -p "$gate"
for profile in reserved combined opal-combined; do
 mkdir -p "$gate/$profile/include"
 printf '%s\n' '#define CONFIG_DEFAULT_CONSOLE_LOGLEVEL 0' \
  '#define CONFIG_MAX_CPUS 4' '#define CONFIG_SMM_APMC_COMMAND_REGISTRY 1' \
  '#define CONFIG_SMM_APMC_COMPOSITION_ATTESTED 1' \
  '#define CONFIG_SMM_APMC_ROUTE_ACPI_CONTROL 1' > "$gate/$profile/include/config.h"
 if test "$profile" != reserved; then
  printf '%s\n' '#define CONFIG_PAYLOAD_SPI_FLASH_CONSOLE 1' \
   '#define CONFIG_BOARD_EMULATION_QEMU_X86_Q35 1' \
   '#define CONFIG_CAPSULE_BROKER_ENDPOINT_PUBLICATION 1' \
   '#define CONFIG_SMM_APMC_ROUTE_SPI_CONSOLE 1' \
   '#define CONFIG_SMM_APMC_ROUTE_CAPSULE_BROKER 1' >> "$gate/$profile/include/config.h"
 fi
 if test "$profile" = opal-combined; then
  printf '%s\n' '#define CONFIG_TCG_OPAL_S3_UNLOCK 1' \
   '#define CONFIG_SMM_APMC_ROUTE_OPAL 1' >> "$gate/$profile/include/config.h"
 fi
 for opt in O0 O2; do
  cc -std=gnu11 -"$opt" -Wall -Wextra -Werror -Wconversion -Wshadow -pthread \
   -fno-builtin -D__TEST__ -D__COREBOOT__ \
   -fsanitize=address,undefined -fno-sanitize-recover=all \
   -include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
   -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
   -I"$gate/$profile/include" -I"$root/src/include" -I"$root/src" \
   -I"$root/src/commonlib/include" -I"$root/src/commonlib/bsd/include" \
   -I"$root/src/arch/x86/include" "$root/tests/cpu/x86/smm_command_test.c" \
   "$root/src/cpu/x86/smm_command.c" -o "$gate/$profile/$opt" \
   > "$gate/$profile/$opt-build.log" 2>&1
  ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
   "$gate/$profile/$opt" > "$gate/$profile/$opt-test.log" 2>&1
  printf '0\n' > "$gate/$profile/$opt.exit"
 done
done
# Actual source header inverses: restore the old collision and choose OPAL's
# reserved value. Both selected owners remain enabled; no route is disabled.
for command in e8 e9; do
 mkdir -p "$gate/alias-$command/cpu/x86"
 sed "s/SMM_APMC_CAPSULE_BROKER 0xe4U/SMM_APMC_CAPSULE_BROKER 0x${command}U/" \
  "$root/src/include/cpu/x86/smm_command.h" > "$gate/alias-$command/cpu/x86/smm_command.h"
 for opt in O0 O2; do
  if cc -std=gnu11 -"$opt" -D__TEST__ -D__COREBOOT__ \
   -include "$root/src/include/kconfig.h" -include "$root/src/include/rules.h" \
   -include "$root/src/commonlib/bsd/include/commonlib/bsd/compiler.h" \
   -I"$gate/opal-combined/include" -I"$gate/alias-$command" \
   -I"$root/src/include" -I"$root/src" -I"$root/src/commonlib/include" \
   -I"$root/src/commonlib/bsd/include" -I"$root/src/arch/x86/include" \
   -c "$root/src/cpu/x86/smm_command.c" -o "$gate/alias-$command/$opt.o" \
   > "$gate/alias-$command/$opt-build.log" 2>&1; then
   exit 1
  fi
  grep -q 'duplicate case' "$gate/alias-$command/$opt-build.log"
 done
done
grep -Eq '^#define CAPSULE_BROKER_APM_COMMAND[[:space:]]+0xe4U$' "$root/src/include/boot/capsule_broker.h"
grep -Eq '^#define PAYLOAD_SPI_CONSOLE_APM_CMD[[:space:]]+0xe8U$' "$root/src/include/console/payload_spi_console.h"
