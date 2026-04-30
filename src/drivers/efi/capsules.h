/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _EDK2_CAPSULES_H_
#define _EDK2_CAPSULES_H_

#include <stdbool.h>

#if CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)

void efi_parse_capsules(void);

void efi_add_capsules_to_bootmem(void);

bool efi_is_disk_capsules_boot(void);
bool efi_capsules_pending(void);

#else

static inline void efi_parse_capsules(void) { }

static inline void efi_add_capsules_to_bootmem(void) { }

static inline bool efi_is_disk_capsules_boot(void) { return false; }
static inline bool efi_capsules_pending(void) { return false; }

#endif

#endif /* _EDK2_CAPSULES_H_ */
