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
	resource_t end;
	resource_t start;

	if (range_entry_tag(r) != BM_MEM_RAM)
		return true;

	/* Section must be aligned at page boundary */
	start = ALIGN_UP(MAX(region->offset, range_entry_base(r)), SECTION_ALIGN);
	end = range_entry_end(r);

	if (start < end && region->size <= end - start) {
		region->offset = (size_t)start;
		return false;
	}

	return true;
}

bool fit_payload_arch(struct prog *payload, struct fit_config_node *config,
		      struct region *kernel, struct region *fdt, struct region *initrd)
{
	uintptr_t entrypoint = config->kernel->entrypoint_address;
	uintptr_t load_address = config->kernel->load_address;

	printk(BIOS_DEBUG, "FIT: Using kernel size of 0x%zx bytes\n", kernel->size);

	/*
	 * The code assumes that bootmem_walk provides a sorted list of memory
	 * regions, starting from the lowest address.
	 * The order of the calls here doesn't matter, as the placement is
	 * enforced in the called functions.
	 * For details check code on top.
	 */
	if (load_address == 0 || entrypoint < load_address ||
	    kernel->size > UINTPTR_MAX - load_address ||
	    entrypoint >= load_address + kernel->size) {
		printk(BIOS_ERR, "FIT: Invalid load/entry range %p+0x%zx entry %p\n",
		       (void *)load_address, kernel->size, (void *)entrypoint);
		return false;
	}

	kernel->offset = load_address;
	if (!bootmem_walk(fit_place_mem, kernel) || kernel->offset != load_address) {
		printk(BIOS_ERR, "FIT: Fixed load range %p+0x%zx is unavailable\n",
		       (void *)load_address, kernel->size);
		return false;
	}

	/* Mark as reserved for future allocations. */
	bootmem_add_range(kernel->offset, kernel->size, BM_MEM_PAYLOAD);

	/* Place FDT and INITRD after kernel. */

	/* Place FDT here. */
	if (kernel->offset > SIZE_MAX - kernel->size)
		return false;
	fdt->offset = kernel->offset + kernel->size;
	if (!bootmem_walk(fit_place_mem, fdt))
		return false;

	/* Mark as reserved for future allocations. */
	bootmem_add_range(fdt->offset, fdt->size, BM_MEM_PAYLOAD);

	/* Now place INITRD. */
	if (config->ramdisk) {
		if (fdt->offset > SIZE_MAX - fdt->size)
			return false;
		initrd->offset = fdt->offset + fdt->size;
		if (!bootmem_walk(fit_place_mem, initrd))
			return false;

		/* Mark as reserved for future allocations. */
		bootmem_add_range(initrd->offset, initrd->size, BM_MEM_PAYLOAD);
	}

	prog_set_entry(payload, (void *)entrypoint, (void *)fdt->offset);

	bootmem_dump_ranges();

	return true;
}
