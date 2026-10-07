#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
include=${1:?retained fixture include directory required}
mkdir -p "$include"
# Follow the existing HOST generated-config convention. These are exactly the
# prior model values; conditionals now use the genuine production header.
printf '%s\n' \
 '#define CONFIG_PAYLOAD_SPI_FLASH_CONSOLE_NATIVE_PREFIX 1' \
 '#define CONFIG_CONSOLE_SPI_FLASH 1' \
 '#define CONFIG_CONSOLE_SPI_FLASH_LATEST_BOOT 1' \
 '#define CONFIG_CONSOLE_SPI_FLASH_BUFFER_SIZE 0x20000' \
 '#define CONFIG_CONSOLE_CBMEM 1' \
 '#define CONFIG_BOARD_EMULATION_QEMU_X86_Q35 1' \
 '#define CONFIG_SMMSTORE 0' \
 '#define CONFIG_SMMSTORE_READ_REGION 1' \
 '#define CONFIG_DEBUG_SMI 0' \
 '#define CONFIG_BOOTBLOCK_CONSOLE 0' \
 '#define CONFIG_QEMU_PFLASH_VOLATILE_LEASE 1' \
 '#define CONFIG_ELOG 0' \
 '#define CONFIG_ROM_SIZE 0xb0000' > "$include/config.h"
