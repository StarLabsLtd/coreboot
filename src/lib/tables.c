/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cbconfig.h>
#include <boardid.h>
#include <boot/coreboot_tables.h>
#include <boot/tables.h>
#include <boot/upl_fdt_table.h>
#include <cbmem.h>
#include <console/console.h>
#include <fw_config.h>
#include <types.h>

void *write_tables(void)
{
	uintptr_t cbtable_start;
	uintptr_t cbtable_end;
	uintptr_t fdt_start = 0;
	uintptr_t fdt_end = 0;
	size_t fdt_capacity = 0;
	bool use_existing_fdt = false;

	cbtable_start = (uintptr_t)cbmem_add(CBMEM_ID_CBTABLE, MAX_COREBOOT_HANDOFF_SIZE);

	if (!cbtable_start) {
		printk(BIOS_ERR, "Could not add CBMEM for coreboot table.\n");
		return NULL;
	}

	if (CONFIG(HANDOFF_UPL_DEVICETREE)) {
		const struct cbmem_entry *fdt_entry = cbmem_entry_find(CBMEM_ID_FDT);
		if (fdt_entry) {
			fdt_start = (uintptr_t)cbmem_entry_start(fdt_entry);
			fdt_capacity = cbmem_entry_size(fdt_entry);
			use_existing_fdt = true;
		} else {
			fdt_start = (uintptr_t)cbmem_add(CBMEM_ID_FDT,
						     MAX_COREBOOT_HANDOFF_SIZE);
			fdt_capacity = MAX_COREBOOT_HANDOFF_SIZE;
		}
		if (!fdt_start) {
			printk(BIOS_ERR, "Could not add CBMEM for UPL FDT.\n");
			return NULL;
		}
	}

	/* Add architecture specific tables. */
	arch_write_tables(cbtable_start);

	/* UPL consumers still need the real coreboot table referenced by the FDT. */
	cbtable_end = write_coreboot_table(cbtable_start);
	size_t cbtable_size = cbtable_end - cbtable_start;

	if (cbtable_size > MAX_COREBOOT_HANDOFF_SIZE) {
		printk(BIOS_ERR, "%s: coreboot table didn't fit (%zx/%x)\n",
			__func__, cbtable_size, MAX_COREBOOT_HANDOFF_SIZE);
	}

	printk(BIOS_DEBUG, "coreboot table: %zd bytes.\n", cbtable_size);

	if (CONFIG(HANDOFF_UPL_DEVICETREE)) {
		fdt_end = write_upl_fdt_table(fdt_start, fdt_capacity,
					      use_existing_fdt);
		size_t fdt_size = fdt_end - fdt_start;

		if (fdt_size > MAX_COREBOOT_HANDOFF_SIZE) {
			printk(BIOS_ERR, "%s: UPL FDT didn't fit (%zx/%x)\n",
				__func__, fdt_size, MAX_COREBOOT_HANDOFF_SIZE);
		}

		printk(BIOS_DEBUG, "UPL FDT: %zd bytes.\n", fdt_size);
	}

	/* Print CBMEM sections */
	cbmem_list();
	return (void *)cbtable_start;
}
