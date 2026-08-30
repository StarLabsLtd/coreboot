/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _EDK2_CAPSULES_H_
#define _EDK2_CAPSULES_H_

#if CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)

void efi_parse_capsules(void);

void efi_add_capsules_to_bootmem(void);

bool efi_capsule_coalesce_span(uintptr_t *base, size_t *size);

#else

static inline void efi_parse_capsules(void) { }

static inline void efi_add_capsules_to_bootmem(void) { }

static inline bool efi_capsule_coalesce_span(uintptr_t *base, size_t *size)
{
	(void)base;
	(void)size;
	return false;
}

#endif

#endif /* _EDK2_CAPSULES_H_ */
