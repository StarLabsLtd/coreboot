/* SPDX-License-Identifier: GPL-2.0-only */

/* Include the unit to exercise its private, parsed-capsule state. */
#define main coreboot_main
#include "../../src/drivers/efi/capsules.c"
#undef main

static struct lb_range records[MAX_CAPSULES];
static size_t next_record;

int printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
	return 0;
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	header->table_entries++;
	return (struct lb_record *)&records[next_record++];
}

int main(void)
{
	struct lb_header header = { 0 };

	memset(records, 0, sizeof(records));
	next_record = 0;
	uefi_capsule_count = 2;
	uefi_capsules[0] = (struct memory_range) {
		.base = 0x12345000,
		.len = 0x3210,
	};
	uefi_capsules[1] = (struct memory_range) {
		.base = 0x87654000,
		.len = 0x4560,
	};

	lb_efi_capsules(&header);

	if (header.table_entries != 2 || next_record != 2)
		return 1;
	if (records[0].tag != LB_TAG_CAPSULE || records[1].tag != LB_TAG_CAPSULE)
		return 2;
	if (records[0].size != sizeof(records[0]) ||
	    records[1].size != sizeof(records[1]))
		return 3;
	if (records[0].range_start != uefi_capsules[0].base ||
	    records[0].range_size != uefi_capsules[0].len)
		return 4;
	if (records[1].range_start != uefi_capsules[1].base ||
	    records[1].range_size != uefi_capsules[1].len)
		return 5;

	return 0;
}
