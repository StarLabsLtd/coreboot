/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * Exercise the real table initializer, record allocator and checksum finalizer.
 * The paired component test does not retain unrelated platform table producers.
 */
#include "../../src/lib/coreboot_table.c"

struct lb_header *boot_private_table_initialize(uintptr_t base);
void boot_private_table_memory(struct lb_header *header);
void boot_private_table_finish(struct lb_header *header);

struct lb_header *boot_private_table_initialize(uintptr_t base)
{
	return lb_table_init(base);
}

void boot_private_table_memory(struct lb_header *header)
{
	bootmem_write_memory_table(lb_memory(header));
}

void boot_private_table_finish(struct lb_header *header)
{
	lb_table_fini(header);
}
