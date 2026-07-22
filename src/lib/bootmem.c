/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <bootmem.h>
#include <boot/upl_fdt_table.h>
#include <cbmem.h>
#include <console/console.h>
#include <device/resource.h>
#include <drivers/efi/capsules.h>
#include <symbols.h>
#include <types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int initialized;
static int table_written;
static struct memranges bootmem;
static struct memranges bootmem_os;

#if ENV_TEST
void bootmem_reset_for_test(void)
{
	if (initialized) {
		memranges_teardown(&bootmem);
		memranges_teardown(&bootmem_os);
	}

	initialized = 0;
	table_written = 0;
}
#endif

struct range_strings {
	enum bootmem_type tag;
	const char *str;
};

static const struct range_strings type_strings[] = {
	{ BM_MEM_RAM, "RAM" },
	{ BM_MEM_RESERVED, "RESERVED" },
	{ BM_MEM_ACPI, "ACPI" },
	{ BM_MEM_NVS, "NVS" },
	{ BM_MEM_UNUSABLE, "UNUSABLE" },
	{ BM_MEM_VENDOR_RSVD, "VENDOR RESERVED" },
	{ BM_MEM_BL31, "BL31" },
	{ BM_MEM_OPENSBI, "OPENSBI" },
	{ BM_MEM_TABLE, "CONFIGURATION TABLES" },
	{ BM_MEM_SOFT_RESERVED, "SOFT RESERVED" },
	{ BM_MEM_RAMSTAGE, "RAMSTAGE" },
	{ BM_MEM_PAYLOAD, "PAYLOAD" },
	{ BM_MEM_TAG, "TAG STORAGE" },
};

static const char *bootmem_range_string(const enum bootmem_type tag)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(type_strings); i++) {
		if (type_strings[i].tag == tag)
			return type_strings[i].str;
	}

	return "UNKNOWN!";
}

static int bootmem_is_initialized(void)
{
	return initialized;
}

static int bootmem_memory_table_written(void)
{
	return table_written;
}

/* Platform hook to add bootmem areas the platform / board controls. */
void __attribute__((weak)) bootmem_platform_add_ranges(void)
{
}

/* Convert bootmem tag to LB_MEM tag */
static uint32_t bootmem_to_lb_tag(const enum bootmem_type tag)
{
	switch (tag) {
	case BM_MEM_RAM:
		return LB_MEM_RAM;
	case BM_MEM_RESERVED:
		return LB_MEM_RESERVED;
	case BM_MEM_ACPI:
		return LB_MEM_ACPI;
	case BM_MEM_NVS:
		return LB_MEM_NVS;
	case BM_MEM_UNUSABLE:
		return LB_MEM_UNUSABLE;
	case BM_MEM_VENDOR_RSVD:
		return LB_MEM_VENDOR_RSVD;
	case BM_MEM_OPENSBI:
		return LB_MEM_RESERVED;
	case BM_MEM_BL31:
		return LB_MEM_RESERVED;
	case BM_MEM_TABLE:
		return LB_MEM_TABLE;
	case BM_MEM_SOFT_RESERVED:
		return LB_MEM_SOFT_RESERVED;
	case BM_MEM_TAG:
		return LB_MEM_TAG;
	default:
		printk(BIOS_ERR, "Unsupported tag %u\n", tag);
		return LB_MEM_RESERVED;
	}
}

static void bootmem_init(void)
{
	const unsigned long cacheable = IORESOURCE_CACHEABLE;
	const unsigned long reserved = IORESOURCE_RESERVE;
	const unsigned long soft_reserved = IORESOURCE_SOFT_RESERVE;
	struct memranges *bm = &bootmem;

	initialized = 1;

	/*
	 * Fill the memory map out. The order of operations is important in
	 * that each overlapping range will take over the next. Therefore,
	 * add cacheable resources as RAM then add the reserved resources.
	 */
	memranges_init(bm, cacheable, cacheable, BM_MEM_RAM);
	memranges_add_resources(bm, reserved, reserved, BM_MEM_RESERVED);
	memranges_add_resources(bm, soft_reserved, soft_reserved, BM_MEM_SOFT_RESERVED);
	memranges_clone(&bootmem_os, bm);

	/* Add memory used by CBMEM. */
	cbmem_add_bootmem();

	efi_add_capsules_to_bootmem();

	bootmem_add_range((uintptr_t)_stack, REGION_SIZE(stack),
			  BM_MEM_RAMSTAGE);
	bootmem_add_range((uintptr_t)_program, REGION_SIZE(program),
			  BM_MEM_RAMSTAGE);

	bootmem_arch_add_ranges();
	bootmem_platform_add_ranges();
}

void bootmem_add_range(uint64_t start, uint64_t size,
		       const enum bootmem_type tag)
{
	assert(tag > BM_MEM_FIRST && tag < BM_MEM_LAST);
	assert(bootmem_is_initialized());

	memranges_insert(&bootmem, start, size, tag);
	if (tag <= BM_MEM_OS_CUTOFF) {
		/* Can't change OS tables anymore after they are written out. */
		assert(!bootmem_memory_table_written());
		memranges_insert(&bootmem_os, start, size, tag);
	};
}

int bootmem_add_range_from(uint64_t start, uint64_t size, const enum bootmem_type new_tag,
			   const enum bootmem_type from_tag)
{
	assert(new_tag != from_tag);

	if (!bootmem_region_targets_type(start, size, from_tag)) {
		printk(BIOS_ERR, "%s: Failed to add the range [%#llx, %#llx)"
		       " from tag %s to %s\n", __func__, start, start + size,
		       bootmem_range_string(from_tag), bootmem_range_string(new_tag));
		return -1;
	}

	bootmem_add_range(start, size, new_tag);

	return 0;
}

void bootmem_write_memory_table(struct lb_memory *mem)
{
	const struct range_entry *r;
	struct lb_memory_range *lb_r;

	lb_r = &mem->map[0];

	if (!bootmem_is_initialized())
		bootmem_init();
	bootmem_dump_ranges();

	memranges_each_entry(r, &bootmem_os) {
		lb_r->start = range_entry_base(r);
		lb_r->size = range_entry_size(r);
		lb_r->type = bootmem_to_lb_tag(range_entry_tag(r));

		lb_r++;
		mem->size += sizeof(struct lb_memory_range);
	}

	table_written = 1;
}

static uint64_t bootmem_top_of_dram_below(uint64_t limit)
{
	const struct range_entry *r;
	uint64_t top = 0;

	if (!bootmem_is_initialized())
		bootmem_init();

	/*
	 * Match UefiPayloadPkg's TOLUD estimate. Usable ranges raise the limit,
	 * while adjacent reserved ranges account for stolen memory, TSEG and
	 * other DRAM hidden from the OS. Disjoint ranges below 4 GiB are MMIO.
	 */
	memranges_each_entry(r, &bootmem_os) {
		const uint64_t base = range_entry_base(r);
		const uint64_t end = range_entry_end(r);

		if (end > limit || range_entry_tag(r) == BM_MEM_UNUSABLE)
			continue;

		switch (range_entry_tag(r)) {
		case BM_MEM_RAM:
		case BM_MEM_ACPI:
		case BM_MEM_NVS:
			top = MAX(top, end);
			break;
		default:
			if (base == top)
				top = end;
			break;
		}
	}

	return top;
}

uint64_t bootmem_top_of_low_dram(void)
{
	return bootmem_top_of_dram_below(1ULL << 32);
}

uint64_t bootmem_top_of_dram(void)
{
	return bootmem_top_of_dram_below(UINT64_MAX);
}

// Write memory ranges in devicetree of UPL handoff
void upl_fdt_add_memory(struct device_tree *tree)
{
	struct {
		u64 start;
		u64 end;
	} table_exclusions[2];
	size_t table_exclusion_count = 0;
	const uint64_t top_of_low_dram = ENV_X86 ? bootmem_top_of_low_dram() : 0;
	const uint64_t top_of_dram = ENV_X86 ? bootmem_top_of_dram() : 0;
	const uint64_t low_address_limit = 1ULL << 32;

	printk(BIOS_DEBUG, "Write UPL FDT memory entries\n");
	if (ENV_X86)
		printk(BIOS_DEBUG, "UPL FDT: TOLUD is 0x%llx, TOUUD is 0x%llx\n",
		       top_of_low_dram, top_of_dram);
	if (!bootmem_is_initialized())
		bootmem_init();
	bootmem_dump_ranges();

	/* UPL x86 address and size fields are both 64-bit. */
	u32 addr_cells = 2;
	u32 size_cells = 2;

	struct device_tree_node *rsvd_node;
	rsvd_node = dt_find_node_by_path(tree, "/reserved-memory", &addr_cells, &size_cells, 1);
	if (!rsvd_node)
		return;

	// Binding doc says this should have the same #{address,size}-cells as the root.
	dt_add_u32_prop(rsvd_node, "#address-cells", addr_cells);
	dt_add_u32_prop(rsvd_node, "#size-cells", size_cells);
	// Binding doc says this should be empty (1:1 mapping from root).
	dt_add_bin_prop(rsvd_node, "ranges", NULL, 0);

	/*
	 * EDK2 gives ACPI and SMBIOS their own allocation HOBs. Exclude those
	 * entries from the generic CBMEM reservation to avoid overlapping HOBs.
	 */
	if (CONFIG(HANDOFF_UPL_DEVICETREE) && CONFIG(HAVE_ACPI_TABLES)) {
		const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_ACPI);

		if (entry) {
			table_exclusions[table_exclusion_count].start =
				(uintptr_t)cbmem_entry_start(entry);
			table_exclusions[table_exclusion_count].end =
				table_exclusions[table_exclusion_count].start +
				cbmem_entry_size(entry);
			table_exclusion_count++;
		}
	}

	if (CONFIG(HANDOFF_UPL_DEVICETREE) && CONFIG(GENERATE_SMBIOS_TABLES)) {
		const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_SMBIOS);

		if (entry) {
			table_exclusions[table_exclusion_count].start =
				(uintptr_t)cbmem_entry_start(entry);
			table_exclusions[table_exclusion_count].end =
				table_exclusions[table_exclusion_count].start +
				cbmem_entry_size(entry);
			table_exclusion_count++;
		}
	}

	if (table_exclusion_count == 2 &&
	    table_exclusions[0].start > table_exclusions[1].start) {
		const u64 start = table_exclusions[0].start;
		const u64 end = table_exclusions[0].end;

		table_exclusions[0] = table_exclusions[1];
		table_exclusions[1].start = start;
		table_exclusions[1].end = end;
	}

	const struct range_entry *r;
	memranges_each_entry(r, &bootmem_os) {
		char node_name[100];
		u64 addr = range_entry_base(r);
		u64 size = range_entry_size(r);
		struct device_tree_node *node = NULL;

		if (addr == CONFIG_ECAM_MMCONF_BASE_ADDRESS &&
		    size == CONFIG_ECAM_MMCONF_LENGTH) {
			snprintf(node_name, sizeof(node_name), "mmio@%llx", addr);
			const char *node_names_mmio[] = { node_name, NULL };
			node = dt_find_node(rsvd_node, node_names_mmio, NULL, NULL, 1);
			dt_add_bin_prop(node, "no-map", NULL, 0);
			dt_add_reg_prop(node, &addr, &size, 1, addr_cells, size_cells);
			continue;
		}

		switch (range_entry_tag(r)) {
		case BM_MEM_RAM:
		case BM_MEM_RAMSTAGE:
		case BM_MEM_PAYLOAD:
			snprintf(node_name, 100, "/memory@%llx", addr);
			node = dt_find_node_by_path(tree, node_name, NULL, NULL, 1);
			dt_add_string_prop(node, "device_type", "memory");
			break;
		case BM_MEM_ACPI:
			snprintf(node_name, 100, "acpi-reclaim@%llx", addr);
			const char *node_names_acpi[] = { node_name, NULL };
			node = dt_find_node(rsvd_node, node_names_acpi, NULL, NULL, 1);
			dt_add_bin_prop(node, "no-map", NULL, 0);
			break;
		case BM_MEM_NVS:
			snprintf(node_name, 100, "acpi-nvs@%llx", addr);
			const char *node_names_nvs[] = { node_name, NULL };
			node = dt_find_node(rsvd_node, node_names_nvs, NULL, NULL, 1);
			dt_add_bin_prop(node, "no-map", NULL, 0);
			break;
		case BM_MEM_VENDOR_RSVD:
		case BM_MEM_UNUSABLE:
		case BM_MEM_RESERVED:
		case BM_MEM_SOFT_RESERVED:
		case BM_MEM_OPENSBI:
		case BM_MEM_BL31:
		default:
			if (ENV_X86 && ((addr >= top_of_low_dram &&
					 addr < low_address_limit) || addr >= top_of_dram))
				snprintf(node_name, sizeof(node_name), "mmio@%llx", addr);
			else
				snprintf(node_name, sizeof(node_name), "memory@%llx", addr);
			const char *node_names[] = { node_name, NULL };
			node = dt_find_node(rsvd_node, node_names, NULL, NULL, 1);
			dt_add_bin_prop(node, "no-map", NULL, 0);
			break;
		case BM_MEM_TABLE: {
			u64 cursor = addr;
			const u64 end = addr + size;

			for (size_t i = 0; i < table_exclusion_count; i++) {
				const u64 exclusion_start = table_exclusions[i].start;
				const u64 exclusion_end = table_exclusions[i].end;

				if (exclusion_end <= cursor || exclusion_start >= end)
					continue;

				if (exclusion_start > cursor) {
					const u64 reserved_size = exclusion_start - cursor;

					snprintf(node_name, sizeof(node_name),
						 "coreboot@%llx", cursor);
					const char *table_gap_nodes[] = { node_name, NULL };
					node = dt_find_node(rsvd_node, table_gap_nodes,
							    NULL, NULL, 1);
					dt_add_bin_prop(node, "no-map", NULL, 0);
					dt_add_reg_prop(node, &cursor, &reserved_size, 1,
							addr_cells, size_cells);
				}

				cursor = MAX(cursor, MIN(end, exclusion_end));
			}

			if (cursor < end) {
				const u64 reserved_size = end - cursor;

				snprintf(node_name, sizeof(node_name),
					 "coreboot@%llx", cursor);
				const char *table_tail_nodes[] = { node_name, NULL };
				node = dt_find_node(rsvd_node, table_tail_nodes,
						    NULL, NULL, 1);
				dt_add_bin_prop(node, "no-map", NULL, 0);
				dt_add_reg_prop(node, &cursor, &reserved_size, 1,
						addr_cells, size_cells);
			}
			continue;
		}
		}
		dt_add_reg_prop(node, &addr, &size, 1, addr_cells, size_cells);
	}
}

void upl_fdt_refresh_memory(struct device_tree *tree)
{
	struct device_tree_node *node;
	struct device_tree_node *memory_node;
	const struct range_entry *r;

	/* The recovered EDK2 parser requires one memory@ node per RAM tuple. */
	do {
		memory_node = NULL;
		list_for_each(node, tree->root->children, list_node) {
			const char *device_type = dt_find_string_prop(node, "device_type");
			if (device_type && !strcmp(device_type, "memory")) {
				memory_node = node;
				break;
			}
		}
		if (memory_node)
			list_remove(&memory_node->list_node);
	} while (memory_node);

	memranges_each_entry(r, &bootmem) {
		char node_name[64];
		u64 address;
		u64 size;

		if (range_entry_tag(r) != BM_MEM_RAM &&
		    range_entry_tag(r) != BM_MEM_PAYLOAD)
			continue;

		address = range_entry_base(r);
		size = range_entry_size(r);
		snprintf(node_name, sizeof(node_name), "/memory@%llx", address);
		node = dt_find_node_by_path(tree, node_name, NULL, NULL, 1);
		if (!node) {
			printk(BIOS_ERR, "%s: %s node is null\n", __func__, node_name);
			continue;
		}

		dt_add_string_prop(node, "device_type", "memory");
		dt_add_reg_prop(node, &address, &size, 1, 2, 2);
	}
}

void bootmem_dump_ranges(void)
{
	int i;
	const struct range_entry *r;

	i = 0;
	memranges_each_entry(r, &bootmem) {
		printk(BIOS_DEBUG, "%2d. %016llx-%016llx: %s\n",
			i, range_entry_base(r), range_entry_end(r) - 1,
			bootmem_range_string(range_entry_tag(r)));
		i++;
	}
}

bool bootmem_walk_os_mem(range_action_t action, void *arg)
{
	const struct range_entry *r;

	assert(bootmem_is_initialized());

	memranges_each_entry(r, &bootmem_os) {
		if (!action(r, arg))
			return true;
	}

	return false;
}

bool bootmem_walk(range_action_t action, void *arg)
{
	const struct range_entry *r;

	assert(bootmem_is_initialized());

	memranges_each_entry(r, &bootmem) {
		if (!action(r, arg))
			return true;
	}

	return false;
}

int bootmem_region_targets_type(uint64_t start, uint64_t size,
				enum bootmem_type dest_type)
{
	const struct range_entry *r;
	uint64_t end = start + size;

	memranges_each_entry(r, &bootmem) {
		/* All further bootmem entries are beyond this range. */
		if (end <= range_entry_base(r))
			break;

		if (start >= range_entry_base(r) && end <= range_entry_end(r)) {
			if (range_entry_tag(r) == dest_type)
				return 1;
		}
	}
	return 0;
}

void *bootmem_allocate_buffer(size_t size)
{
	const struct range_entry *r;
	const struct range_entry *region;
	/* All allocated buffers fall below the 32-bit boundary. */
	const resource_t max_addr = 1ULL << 32;
	resource_t begin;
	resource_t end;

	if (!bootmem_is_initialized()) {
		printk(BIOS_ERR, "%s: lib uninitialized!\n", __func__);
		return NULL;
	}

	/* 4KiB alignment. */
	size = ALIGN_UP(size, 4096);
	region = NULL;
	memranges_each_entry(r, &bootmem) {
		if (range_entry_base(r) >= max_addr)
			break;

		if (range_entry_size(r) < size)
			continue;

		if (range_entry_tag(r) != BM_MEM_RAM)
			continue;

		end = range_entry_end(r);
		if (end > max_addr)
			end = max_addr;

		if ((end - range_entry_base(r)) < size)
			continue;

		region = r;
	}

	if (region == NULL)
		return NULL;

	/* region now points to the highest usable region for the given size. */
	end = range_entry_end(region);
	if (end > max_addr)
		end = max_addr;
	begin = end - size;

	/* Mark buffer as unusable for future buffer use. */
	bootmem_add_range(begin, size, BM_MEM_PAYLOAD);

	return (void *)(uintptr_t)begin;
}
