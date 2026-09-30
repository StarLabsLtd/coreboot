/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__

#include <commonlib/bsd/helpers.h>
#include <cpu/x86/msr.h>
#include <cpu/x86/mtrr.h>
#include <device/pci_ops.h>
#include <intelblocks/msr.h>
#include <intelblocks/systemagent.h>
#include <soc/pci_devs.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "authvar_protected_region.h"

/* Intel Core Ultra H/U CFG/MEM registers, public document 795258:
 * TSEGMB/BGSM bits 31:20 are the base, bit 0 locks each register.
 * TSEGMB requires natural 8 MiB alignment. No register is programmed here.
 */
#define TSEG_ADDRESS_MASK 0xfff00000U
#define TSEG_REGISTER_LOCK 1U

struct protection_snapshot {
	uint32_t tseg;
	uint32_t gsm;
	msr_t capability;
	msr_t base;
	msr_t mask;
};

static bool protection_read(struct protection_snapshot *snapshot)
{
	snapshot->capability = rdmsr(MTRR_CAP_MSR);
	if ((snapshot->capability.lo & (MTRR_CAP_SMRR | SMRR_LOCK_SUPPORTED)) !=
	    (MTRR_CAP_SMRR | SMRR_LOCK_SUPPORTED))
		return false;
	snapshot->tseg = pci_read_config32(SA_DEV_ROOT, TSEG);
	snapshot->gsm = pci_read_config32(SA_DEV_ROOT, BGSM);
	snapshot->base = rdmsr(IA32_SMRR_PHYS_BASE);
	snapshot->mask = rdmsr(IA32_SMRR_PHYS_MASK);
	return true;
}

enum cb_err starbook_mtl_authvar_protected_region_read(struct region *region)
{
	struct protection_snapshot first = {0}, second = {0};
	uint32_t base, end, size;

	if (!region || !protection_read(&first) || !protection_read(&second) ||
	    memcmp(&first, &second, sizeof(first)) ||
	    !(first.tseg & TSEG_REGISTER_LOCK) || !(first.gsm & TSEG_REGISTER_LOCK) ||
	    ((first.tseg | first.gsm) & ~(TSEG_ADDRESS_MASK | TSEG_REGISTER_LOCK)))
		return CB_ERR;
	base = first.tseg & TSEG_ADDRESS_MASK;
	end = first.gsm & TSEG_ADDRESS_MASK;
	if (!base || end <= base || (base & (8U * MiB - 1U)))
		return CB_ERR;
	size = end - base;
	if ((size & (size - 1U)) || (base & (size - 1U)) ||
	    first.base.hi || first.mask.hi || first.base.lo != (base | MTRR_TYPE_WRBACK) ||
	    first.mask.lo != ((~(size - 1U) & 0xfffff000U) |
		MTRR_PHYS_MASK_VALID | SMRR_PHYS_MASK_LOCK))
		return CB_ERR;
	*region = (struct region){ .offset = base, .size = size };
	return CB_SUCCESS;
}
