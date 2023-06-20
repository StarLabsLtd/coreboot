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
	uintptr_t payload_arg_start = (uintptr_t)cbmem_add(CBMEM_ID_CBTABLE, MAX_COREBOOT_HANDOFF_SIZE);

	if (!payload_arg_start) {
		printk(BIOS_ERR, "Could not add CBMEM for coreboot table.\n");
		return NULL;
	}

	/* Add architecture specific tables. */
	arch_write_tables(payload_arg_start);

	uintptr_t payload_arg_end = 0;
	if (CONFIG(HANDOFF_COREBOOT_TABLES)) {
		/* Write the coreboot table. */
		payload_arg_end = write_coreboot_table(payload_arg_start);
	}
	else if (CONFIG(HANDOFF_UPL_DEVICETREE)) {
		payload_arg_end = write_upl_fdt_table(payload_arg_start);
	}
	size_t payload_arg_size = payload_arg_end - payload_arg_start;

	if (payload_arg_size > MAX_COREBOOT_HANDOFF_SIZE) {
		printk(BIOS_ERR, "%s: coreboot table didn't fit (%zx/%x)\n",
			__func__, payload_arg_size, MAX_COREBOOT_HANDOFF_SIZE);
	}

	printk(BIOS_DEBUG, "coreboot table: %zd bytes.\n", payload_arg_size);

	/* Print CBMEM sections */
	cbmem_list();
	return (void *)payload_arg_start;
}
