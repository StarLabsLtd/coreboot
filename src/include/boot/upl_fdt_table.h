/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef UPL_FDT_TABLE_H
#define UPL_FDT_TABLE_H

#include <types.h>
#include <commonlib/device_tree.h>
#include <commonlib/region.h>

/* Add references to each secondary image. */
void upl_fdt_add_secondary(struct device_tree *tree, const char *name, struct region *secondary);
uintptr_t write_upl_fdt_table(uintptr_t table_start, size_t table_capacity,
			      bool use_existing_fdt);

/*
 * Adds a serial node using parent_node as a parent
 * returns empty string or relative path starting with "/" to the serial node which in turn
 * is usually used to put it into a stdout-path property inside the /chosen node.
 */
const char *upl_fdt_add_serial(struct device_tree_node *parent_node);
void upl_fdt_add_memory(struct device_tree *tree);

#endif
