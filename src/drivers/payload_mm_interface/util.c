/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <payload_mm_interface.h>
#include <types.h>

void payload_mm_get_reserved_region(uintptr_t *tseg_base, size_t *tseg_size)
{
	smm_subregion(SMM_SUBREGION_PAYLOAD, tseg_base, tseg_size);
}
