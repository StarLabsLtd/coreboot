/* SPDX-License-Identifier: GPL-2.0-only */


#ifndef __QEMU_MEMORY_H_
#define __QEMU_MEMORY_H_

#include <stddef.h>

unsigned long qemu_get_high_memory_size(void);
unsigned long qemu_get_memory_size(void);
size_t mainboard_cbmem_top_reservation_size(void);

#endif
