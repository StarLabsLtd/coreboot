/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/psp.h>
#include <amdblocks/root_complex.h>
#include <amdblocks/smn.h>
#include <console/console.h>
#include <device/mmio.h>
#include <types.h>
#include "psp_def.h"

#define IOHC_MISC_PSP_MMIO_REG		0x2e0

static uint64_t get_mmio_mask(uint16_t offset)
{
	const struct non_pci_mmio_reg *mmio_regs;
	size_t reg_count;
	mmio_regs = get_iohc_non_pci_mmio_regs(&reg_count);

	for (size_t i = 0; i < reg_count; i++) {
		if (mmio_regs[i].iohc_misc_offset == offset)
			return mmio_regs[i].mask;
	}

	printk(BIOS_ERR, "No PSP MMIO register description found.\n");
	return 0;
}

#define PSP_MMIO_LOCK	BIT(8)

/* Getting the PSP MMIO base from the domain resources only works in ramstage, but not in SMM,
   so we have to read this from the hardware registers */
static uintptr_t get_psp_mmio_base(void)
{
	static uintptr_t psp_mmio_base;
	const struct domain_iohc_info *iohc;
	size_t iohc_count;

	if (psp_mmio_base)
		return psp_mmio_base;

	iohc = get_iohc_info(&iohc_count);
	const uint64_t psp_mmio_mask = get_mmio_mask(IOHC_MISC_PSP_MMIO_REG);

	if (!psp_mmio_mask)
		return 0;

	for (size_t i = 0; i < iohc_count; i++) {
		uint64_t reg64 = smn_read64(iohc[i].misc_smn_base | IOHC_MISC_PSP_MMIO_REG);

		if (!(reg64 & IOHC_MMIO_EN))
			continue;

		const uint64_t base = reg64 & psp_mmio_mask;

		if (ENV_X86_32 && base >= 4ull * GiB) {
			printk(BIOS_WARNING, "PSP MMIO base above 4GB.\n");
			continue;
		}

		/* If the PSP MMIO base is enabled but the register isn't locked, set the lock
		   bit. This shouldn't happen, but better be a bit too careful here */
		if (!(reg64 & PSP_MMIO_LOCK)) {
			printk(BIOS_WARNING, "Enabled PSP MMIO in domain %zu isn't locked. "
					     "Locking it.\n", i);
			reg64 |= PSP_MMIO_LOCK;
			/* Since the lock bit lives in the lower one of the two 32 bit SMN
			   registers, we only need to write that one to lock it */
			smn_write32(iohc[i].misc_smn_base | IOHC_MISC_PSP_MMIO_REG,
				    reg64 & 0xffffffff);
		}

		psp_mmio_base = base;
	}

	if (!psp_mmio_base)
		printk(BIOS_ERR, "No usable PSP MMIO found.\n");

	return psp_mmio_base;
}

uintptr_t get_ccp_mmio_base(void)
{
	const uint16_t offset = 0x2d8;
	const uint64_t mask = get_mmio_mask(offset);
	size_t count;
	const struct domain_iohc_info *iohc = get_iohc_info(&count);

	if (!mask)
		return 0;
	for (size_t i = 0; i < count; i++) {
		uint32_t addr = iohc[i].misc_smn_base | offset;
		uint64_t reg = smn_read64(addr);
		uint64_t base = reg & mask;

		if (!(reg & IOHC_MMIO_EN) || !base || (ENV_X86_32 && base >= 4ull * GiB))
			continue;
		if (!(reg & PSP_MMIO_LOCK)) {
			smn_write32(addr, (uint32_t)reg | PSP_MMIO_LOCK);
			if (!(smn_read32(addr) & PSP_MMIO_LOCK))
				return 0;
		}
		return base;
	}
	return 0;
}

uint64_t psp_get_base(void)
{
	return get_psp_mmio_base();
}

uint32_t psp_read32(uint64_t base, uint32_t offset)
{
	return read32p((uintptr_t)(base | offset));
}

void psp_write32(uint64_t base, uint32_t offset, uint32_t data)
{
	write32p((uintptr_t)(base | offset), data);
}
