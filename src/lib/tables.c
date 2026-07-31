/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cbconfig.h>
#include <boardid.h>
#include <boot/coreboot_tables.h>
#include <boot/tables.h>
#include <cbmem.h>
#include <console/console.h>
#include <fw_config.h>
#include <types.h>

void *write_tables(void)
{
	uintptr_t cbtable_start;
	uintptr_t cbtable_end;

	cbtable_start = (uintptr_t)cbmem_add(CBMEM_ID_CBTABLE, MAX_COREBOOT_HANDOFF_SIZE);

	if (!cbtable_start) {
		printk(BIOS_ERR, "Could not add CBMEM for coreboot table.\n");
		return NULL;
	}

	/* Add architecture specific tables. */
	arch_write_tables(cbtable_start);

	cbtable_end = write_coreboot_table(cbtable_start);
	size_t cbtable_size = cbtable_end - cbtable_start;

	if (cbtable_size > MAX_COREBOOT_HANDOFF_SIZE) {
		printk(BIOS_ERR, "%s: coreboot table didn't fit (%zx/%x)\n",
			__func__, cbtable_size, MAX_COREBOOT_HANDOFF_SIZE);
	}

	printk(BIOS_DEBUG, "coreboot table: %zd bytes.\n", cbtable_size);

	/* Print CBMEM sections */
	cbmem_list();
	return (void *)cbtable_start;
}
