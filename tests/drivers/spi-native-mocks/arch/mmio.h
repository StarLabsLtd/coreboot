/* SPDX-License-Identifier: GPL-2.0-only */
#include <stdint.h>
uint8_t native_flash_read8(const volatile void *address);
void native_flash_write8(volatile void *address, uint8_t value);
#define read8(address) native_flash_read8(address)
#define write8(address, value) native_flash_write8(address, value)
