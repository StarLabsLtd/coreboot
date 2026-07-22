/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <fit.h>

#define SECTION_ALIGN (4 * KiB)

/**
 * Place the region in free memory range.
 *
 * The caller has to set region->offset to the minimum allowed address.
 */
static bool fit_place_mem(const struct range_entry *r, void *arg)
{
	struct region *region = arg;
	resource_t start;

	if (range_entry_tag(r) != BM_MEM_RAM)
		return true;

	/* Section must be aligned at page boundary */
	start = ALIGN_UP(MAX(region->offset, range_entry_base(r)), SECTION_ALIGN);

	if (start + region->size < range_entry_end(r)) {
		region->offset = (size_t)start;
		return false;
	}

	return true;
}

bool fit_payload_arch(struct prog *payload, struct fit_config_node *config,
		      struct region *kernel, struct region *fdt, struct region *initrd)
{
	printk(BIOS_DEBUG, "FIT: Using kernel size of 0x%zx bytes\n", kernel->size);

	/*
	 * The code assumes that bootmem_walk provides a sorted list of memory
	 * regions, starting from the lowest address.
	 * The order of the calls here doesn't matter, as the placement is
	 * enforced in the called functions.
	 * For details check code on top.
	 */
	kernel->offset = config->kernel->load_address;
	if (!bootmem_walk(fit_place_mem, kernel))
		return false;
	if (config->kernel->load_address &&
	    kernel->offset != config->kernel->load_address) {
		printk(BIOS_ERR, "FIT: Failed to place kernel at 0x%zx\n",
		       (size_t)config->kernel->load_address);
		return false;
	}

	/* Mark as reserved for future allocations. */
	bootmem_add_range(kernel->offset, kernel->size, BM_MEM_PAYLOAD);

	/* Place FDT and INITRD after kernel. */

	/* Place FDT here. */
	fdt->offset = kernel->offset + kernel->size;
	if (!bootmem_walk(fit_place_mem, fdt))
		return false;

	/* Mark as reserved for future allocations. */
	bootmem_add_range(fdt->offset, fdt->size, BM_MEM_PAYLOAD);

	/* Now place INITRD. */
	if (config->ramdisk) {
		initrd->offset = kernel->offset + kernel->size;
		if (!bootmem_walk(fit_place_mem, initrd))
			return false;

		/* Mark as reserved for future allocations. */
		bootmem_add_range(initrd->offset, initrd->size, BM_MEM_PAYLOAD);
	}

	uintptr_t entrypoint = config->kernel->entrypoint_address;
	if (!entrypoint)
		entrypoint = kernel->offset;
	prog_set_entry(payload, (void *)entrypoint, (void *)fdt->offset);

	bootmem_dump_ranges();

	return true;
}
