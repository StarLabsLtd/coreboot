/* SPDX-License-Identifier: GPL-2.0-only */

#include <boardid.h>
#include <bootmem.h>
#include <boot/upl_fdt_table.h>
#include <cbmem.h>
#include <commonlib/bsd/compression.h>
#include <commonlib/bsd/helpers.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <device/resource.h>
#include <fit.h>
#include <lib.h>
#include <program_loading.h>
#include <stdlib.h>
#include <string.h>
#include <timestamp.h>

/* Pack the device_tree and place it at given position. */
static bool pack_fdt(struct region *fdt, struct device_tree *dt)
{
	const size_t size = dt_flat_size(dt);

	if (size > region_sz(fdt)) {
		printk(BIOS_ERR, "FIT: FDT grew from 0x%zx to 0x%zx bytes\n",
		       region_sz(fdt), size);
		return false;
	}

	fdt->size = size;
	printk(BIOS_INFO, "FIT: Flattening FDT to %p\n",
	       (void *)fdt->offset);

	dt_flatten(dt, (void *)fdt->offset);
	prog_segment_loaded(fdt->offset, fdt->size, 0);
	return true;
}

/**
 * Extract a node to given regions.
 * Returns true on error, false on success.
 */
static bool extract(struct region *region, struct fit_image_node *node)
{
	void *dst = (void *)region->offset;
	const char *comp_name;
	size_t true_size = 0;

	if (node->size == 0) {
		printk(BIOS_ERR, "The %s size is 0\n", node->name);
		return true;
	}

	switch (node->compression) {
	case CBFS_COMPRESS_NONE:
		comp_name = "Relocating uncompressed";
		break;
	case CBFS_COMPRESS_LZMA:
		comp_name = "Decompressing LZMA";
		break;
	case CBFS_COMPRESS_LZ4:
		comp_name = "Decompressing LZ4";
		break;
	default:
		printk(BIOS_ERR, "Unsupported compression\n");
		return true;
	}

	printk(BIOS_INFO, "FIT: %s %s to %p\n", comp_name, node->name, dst);

	switch (node->compression) {
	case CBFS_COMPRESS_NONE:
		memcpy(dst, node->data, node->size);
		true_size = node->size;
		break;
	case CBFS_COMPRESS_LZMA:
		timestamp_add_now(TS_ULZMA_START);
		true_size = ulzman(node->data, node->size, dst, region->size);
		timestamp_add_now(TS_ULZMA_END);
		break;
	case CBFS_COMPRESS_LZ4:
		timestamp_add_now(TS_ULZ4F_START);
		true_size = ulz4fn(node->data, node->size, dst, region->size);
		timestamp_add_now(TS_ULZ4F_END);
		break;
	default:
		return true;
	}

	if (!true_size) {
		printk(BIOS_ERR, "%s decompression failed!\n",
		       comp_name);
		return true;
	}

	return false;
}

static struct device_tree *unpack_fdt(struct fit_image_node *image_node)
{
	void *data = image_node->data;

	if (image_node->compression != CBFS_COMPRESS_NONE) {
		/* TODO: This is an ugly heuristic for how much the size will
		   expand on decompression, fix once FIT images support storing
		   the real uncompressed size. */
		struct region r = { .offset = 0, .size = image_node->size * 5 };
		data = malloc(r.size);
		r.offset = (uintptr_t)data;
		if (!data || extract(&r, image_node))
			return NULL;
	}

	return fdt_unflatten(data);
}

static bool fit_fdt_is_bounded(const void *fit, size_t data_size)
{
	const struct fdt_header *header = fit;
	size_t fdt_size;

	if (data_size < sizeof(*header)) {
		printk(BIOS_ERR, "FIT: Container is smaller than the FDT header\n");
		return false;
	}

	fdt_size = be32_to_cpu(header->totalsize);
	if (fdt_size < sizeof(*header) || fdt_size > data_size) {
		printk(BIOS_ERR, "FIT: FDT size 0x%zx exceeds container size 0x%zx\n",
		       fdt_size, data_size);
		return false;
	}

	return true;
}

static bool fit_get_total_size(const void *fit, size_t data_size, size_t *fit_size)
{
	struct fdt_property size_prop;
	const struct fdt_header *header = fit;
	size_t fdt_size;
	u32 root;
	u64 size;

	fdt_size = be32_to_cpu(header->totalsize);

	root = fdt_find_node_by_path(fit, "/", NULL, NULL);
	if (!root || !fdt_read_prop(fit, root, "size", &size_prop)) {
		*fit_size = data_size;
		return true;
	}

	if (size_prop.size != sizeof(u32) && size_prop.size != sizeof(u64)) {
		printk(BIOS_ERR, "FIT: Invalid root size width %u\n", size_prop.size);
		return false;
	}

	size = fdt_read_int_prop(&size_prop, size_prop.size / sizeof(u32));
	if (size < fdt_size || size > 256 * MiB ||
	    size > SIZE_MAX || size > data_size) {
		printk(BIOS_ERR,
		       "FIT: Invalid total size 0x%llx for 0x%zx-byte container\n",
		       size, data_size);
		return false;
	}

	*fit_size = size;
	return true;
}

static bool fit_image_is_bounded(const struct fit_image_node *image,
				 const void *fit, size_t fit_size)
{
	uintptr_t base = (uintptr_t)fit;
	uintptr_t data;
	size_t offset;

	if (!image)
		return true;

	data = (uintptr_t)image->data;
	if (data < base)
		return false;

	offset = data - base;
	return offset <= fit_size && image->size <= fit_size - offset;
}

static bool fit_images_overlap(const struct fit_image_node *first,
			       const struct fit_image_node *second)
{
	uintptr_t first_start;
	uintptr_t second_start;

	if (!first || !second || !first->size || !second->size)
		return false;

	first_start = (uintptr_t)first->data;
	second_start = (uintptr_t)second->data;

	if (first_start < second_start)
		return first->size > second_start - first_start;

	return second->size > first_start - second_start;
}

static bool fit_firmware_entry_is_bounded(const struct fit_image_node *firmware)
{
	size_t image_size;

	if (!firmware)
		return true;

	image_size = MAX(firmware->size, firmware->uncompressed_size);
	return firmware->entrypoint_address >= firmware->load_address &&
	       firmware->entrypoint_address - firmware->load_address < image_size;
}

static bool fit_config_is_bounded(const struct fit_config_node *config,
				  const void *fit, size_t fit_size)
{
	const struct fit_image_node *fixed[] = {
		config->firmware, config->kernel, config->fdt, config->ramdisk,
	};
	struct fit_image_chain *chain;
	struct fit_image_chain *other;
	size_t i;
	size_t j;

	if (!fit_image_is_bounded(config->firmware, fit, fit_size) ||
	    !fit_image_is_bounded(config->kernel, fit, fit_size) ||
	    !fit_image_is_bounded(config->fdt, fit, fit_size) ||
	    !fit_image_is_bounded(config->ramdisk, fit, fit_size) ||
	    !fit_firmware_entry_is_bounded(config->firmware))
		return false;

	for (i = 0; i < ARRAY_SIZE(fixed); i++) {
		for (j = 0; j < i; j++) {
			if (fit_images_overlap(fixed[i], fixed[j]))
				return false;
		}
	}

	list_for_each(chain, config->secondary_images, list_node) {
		if (!fit_image_is_bounded(chain->image, fit, fit_size))
			return false;
		for (i = 0; i < ARRAY_SIZE(fixed); i++) {
			if (fit_images_overlap(chain->image, fixed[i]))
				return false;
		}
		list_for_each(other, config->secondary_images, list_node) {
			if (other == chain)
				break;
			if (fit_images_overlap(chain->image, other->image))
				return false;
		}
	}

	list_for_each(chain, config->overlays, list_node) {
		if (!fit_image_is_bounded(chain->image, fit, fit_size))
			return false;
		for (i = 0; i < ARRAY_SIZE(fixed); i++) {
			if (fit_images_overlap(chain->image, fixed[i]))
				return false;
		}
		list_for_each(other, config->secondary_images, list_node) {
			if (fit_images_overlap(chain->image, other->image))
				return false;
		}
		list_for_each(other, config->overlays, list_node) {
			if (other == chain)
				break;
			if (fit_images_overlap(chain->image, other->image))
				return false;
		}
	}

	return true;
}

/**
 * Add coreboot tables, CBMEM information and optional board specific strapping
 * IDs to the device tree loaded via FIT.
 */
static void add_cb_fdt_data(struct device_tree *tree)
{
	u32 addr_cells = 1;
	u32 size_cells = 1;
	u64 reg_addrs[2], reg_sizes[2];
	void *baseptr;
	size_t size;

	static const char *const firmware_path[] = {"firmware", NULL};
	struct device_tree_node *firmware_node = dt_find_node(tree->root,
		firmware_path, &addr_cells, &size_cells, 1);

	/* Need to add 'ranges' to the intermediate node to make 'reg' work. */
	dt_add_bin_prop(firmware_node, "ranges", NULL, 0);

	static const char *const coreboot_path[] = {"coreboot", NULL};
	struct device_tree_node *coreboot_node = dt_find_node(firmware_node,
		coreboot_path, NULL, NULL, 1);

	dt_add_string_prop(coreboot_node, "compatible", "coreboot");

	/* Fetch CB tables from cbmem */
	void *cbtable = cbmem_find(CBMEM_ID_CBTABLE);
	if (!cbtable) {
		printk(BIOS_WARNING, "FIT: No coreboot table found!\n");
		return;
	}

	/* First 'reg' address range is the coreboot table. */
	const struct lb_header *header = cbtable;
	reg_addrs[0] = (uintptr_t)header;
	reg_sizes[0] = header->header_bytes + header->table_bytes;

	/* Second is the CBMEM area (which usually includes the coreboot
	table). */
	if (cbmem_get_region(&baseptr, &size)) {
		printk(BIOS_WARNING, "FIT: CBMEM pointer/size not found!\n");
		return;
	}

	reg_addrs[1] = (uintptr_t)baseptr;
	reg_sizes[1] = size;

	dt_add_reg_prop(coreboot_node, reg_addrs, reg_sizes, 2, addr_cells,
			size_cells);

	/* Expose board ID, SKU ID, and RAM code to payload.*/
	if (board_id() != UNDEFINED_STRAPPING_ID)
		dt_add_u32_prop(coreboot_node, "board-id", board_id());

	if (sku_id() != UNDEFINED_STRAPPING_ID)
		dt_add_u32_prop(coreboot_node, "sku-id", sku_id());

	if (ram_code() != UNDEFINED_STRAPPING_ID)
		dt_add_u32_prop(coreboot_node, "ram-code", ram_code());
}

/* Extract a kernel and (optional) ramdisk from the FIT. This may involve arch-specific quirks. */
static int fit_extract_kernel(struct prog *payload, struct fit_config_node *config,
			       struct device_tree *dt,
			       struct region *code,
			       struct region *fdt,
			       struct region *initrd)
{
	/* Collect information for the architecture-specific placement. */
	code->size = MAX(config->kernel->size, config->kernel->uncompressed_size);
	fdt->size = dt_flat_size(dt);
	initrd->size = config->ramdisk ? config->ramdisk->size : 0;

	/* Invoke arch specific payload placement and fixups */
	if (!fit_payload_arch(payload, config, code, fdt, initrd)) {
		printk(BIOS_ERR, "Failed to find free memory region\n");
		bootmem_dump_ranges();
		return -1;
	}

	if (extract(code, config->kernel) != 0) {
		printk(BIOS_ERR, "Failed to extract kernel\n");
		prog_set_entry(payload, NULL, NULL);
		return -1;
	}

	if (config->ramdisk) {
		/* Update ramdisk location in FDT */
		fit_add_ramdisk(dt, (void *)initrd->offset, initrd->size);

		if (extract(initrd, config->ramdisk) != 0) {
			printk(BIOS_ERR, "Failed to extract initrd\n");
			prog_set_entry(payload, NULL, NULL);
			return -1;
		}
	}

	return 0;
}

/**
 * Place the region in free memory range.
 *
 * The caller has to set region->offset to the minimum allowed address.
 */
static bool fit_place_mem(const struct range_entry *r, void *arg)
{
#define SECTION_ALIGN (4 * KiB)

	struct region *region = arg;
	resource_t start;

	if (range_entry_tag(r) != BM_MEM_RAM)
		return true;

	/* Section must be aligned at page boundary */
	start = ALIGN_UP(MAX(region_offset(region), range_entry_base(r)), SECTION_ALIGN);

	if (start + region_sz(region) < range_entry_end(r)) {
		region->offset = (size_t)start;
		return false;
	}

	return true;
}

static int fit_allocate_firmware(struct fit_config_node *config,
				  struct region *firmware)
{
	printk(BIOS_DEBUG, "FIT: Using firmware size of 0x%zx bytes\n", region_sz(firmware));

	/* Attempt to fetch a satisfactory region. If successful, allocate it. */
	firmware->offset = config->firmware->load_address;
	if (!bootmem_walk(fit_place_mem, firmware)) {
		printk(BIOS_WARNING, "Failed to place firmware.\n");
		return -1;
	}

	if (config->firmware->load_address &&
	    config->firmware->load_address != region_offset(firmware)) {
		printk(BIOS_ERR, "%s requires relocation, which is unsupported.\n",
		       config->firmware->name);
		return -1;
	}

	/* Mark as reserved for future allocations. */
	bootmem_add_range(region_offset(firmware), region_sz(firmware), BM_MEM_PAYLOAD);

	return 0;
}

static int fit_allocate_firmware_fdt(struct prog *payload,
				      struct fit_config_node *config,
				      const struct region *firmware,
				      struct region *fdt)
{
	/* Place FDT after firmware. */
	fdt->offset = region_offset(firmware) + region_sz(firmware);
	if (!bootmem_walk(fit_place_mem, fdt)) {
		printk(BIOS_WARNING, "Failed to place FDT.\n");
		return -1;
	}

	/* Mark as reserved for future allocations. */
	bootmem_add_range(region_offset(fdt), region_sz(fdt), BM_MEM_PAYLOAD);

	uintptr_t entrypoint = config->firmware->entrypoint_address;
	prog_set_entry(payload, (void *)entrypoint, (void *)region_offset(fdt));

	return 0;
}

static int load_secondaries(struct fit_config_node *config, struct device_tree *tree)
{
	struct region secondary_region;
	struct fit_image_node *secondary_image;
	uint32_t secondary_size;

	struct fit_image_chain *image_chain;
	list_for_each(image_chain, config->secondary_images, list_node) {
		secondary_image = image_chain->image;

		/* Attempt to fetch a satisfactory region. If successful, allocate it. */
		secondary_size = MAX(secondary_image->size, secondary_image->uncompressed_size);
		secondary_region = region_create(secondary_image->load_address, secondary_size);
		/* Avoid NULL dereferences, which can cause failures. */
		if (region_offset(&secondary_region) == 0)
			secondary_region.offset = 0x1000;
		if (!bootmem_walk(fit_place_mem, &secondary_region)) {
			printk(BIOS_ERR, "Failed to place %s.\n", secondary_image->name);
			return -1;
		}

		if (secondary_image->load_address && secondary_image->load_address != region_offset(&secondary_region)) {
			printk(BIOS_ERR, "%s requires relocation, which is unsupported.\n",
			       secondary_image->name);
			return -1;
		}

		/* Mark as reserved for future allocations. */
		bootmem_add_range(region_offset(&secondary_region), secondary_region.size, BM_MEM_PAYLOAD);

		if (extract(&secondary_region, secondary_image) != 0) {
			printk(BIOS_ERR, "Failed to extract %s.\n", secondary_image->name);
			return -1;
		}

	}

	return 0;
}

/* Extract a firmware from the FIT. This deviates from kernel extraction, and involves no quirks. */
static int fit_extract_firmware(struct prog *payload, struct fit_config_node *config,
				 struct device_tree *dt,
				 struct region *code,
				 struct region *fdt)
{
	/* Even if Linux is the internal environment of a firmware,
	   the detail of its initrd should probably be hidden */
	assert(!config->ramdisk);

	/* Collect info for fit_allocate_firmware */
	code->size = MAX(config->firmware->size, config->firmware->uncompressed_size);

	if (fit_allocate_firmware(config, code) != 0) {
		printk(BIOS_ERR, "Failed to find free memory region\n");
		bootmem_dump_ranges();
		return -1;
	}

	if (CONFIG(HANDOFF_UPL_DEVICETREE)) {
		upl_fdt_add_reserved_memory(dt, "upl-entry", region_offset(code),
					    ALIGN_UP(region_sz(code), 4 * KiB),
					    "boot-code");
		upl_fdt_refresh_memory(dt);
	}

	fdt->size = dt_flat_size(dt);
	if (fit_allocate_firmware_fdt(payload, config, code, fdt) != 0) {
		printk(BIOS_ERR, "Failed to find free memory region\n");
		bootmem_dump_ranges();
		return -1;
	}

	if (extract(code, config->firmware) != 0) {
		printk(BIOS_ERR, "Failed to extract firmware\n");
		prog_set_entry(payload, NULL, NULL);
		return -1;
	}

	/* EDK2 resolves its FV data-offsets from the preserved FIT itself. */
	if (!CONFIG(HANDOFF_UPL_DEVICETREE) && load_secondaries(config, dt) != 0) {
		printk(BIOS_ERR, "Failed to extract secondary firmware images\n");
		prog_set_entry(payload, NULL, NULL);
		return -1;
	}

	return 0;
}

/*
 * Parse the uImage FIT, choose a configuration and extract images.
 */
void fit_payload(struct prog *payload, void *data, size_t data_size)
{
	struct device_tree *dt = NULL;
	struct region code = {0}, fdt = {0}, initrd = {0};
	void *upl_fit = NULL;
	size_t fit_size = 0;

	printk(BIOS_INFO, "FIT: Examine payload %s\n", payload->name);
	if (!fit_fdt_is_bounded(data, data_size))
		return;

	struct fit_config_node *config = fit_load(data);
	if (!config) {
		printk(BIOS_ERR, "Could not load FIT\n");
		return;
	}

	if (CONFIG(HANDOFF_UPL_DEVICETREE) && config->firmware) {
		if (!fit_get_total_size(data, data_size, &fit_size) ||
		    !fit_config_is_bounded(config, data, fit_size)) {
			printk(BIOS_ERR, "FIT: UPL image ranges are invalid\n");
			return;
		}

		upl_fit = bootmem_allocate_buffer(fit_size);
		if (!upl_fit) {
			printk(BIOS_ERR, "FIT: Unable to preserve UPL FIT\n");
			return;
		}
		memcpy(upl_fit, data, fit_size);
		printk(BIOS_INFO, "FIT: Preserved 0x%zx-byte UPL FIT at %p\n",
		       fit_size, upl_fit);
	}

	if (config->fdt)
		dt = unpack_fdt(config->fdt);
	else if (CONFIG(HANDOFF_UPL_DEVICETREE) && config->firmware)
		dt = fdt_unflatten(cbmem_find(CBMEM_ID_FDT));
	if (!dt) {
		printk(BIOS_ERR, "Failed to unflatten the FDT.\n");
		return;
	}

	struct fit_image_chain *overlay_chain;
	list_for_each(overlay_chain, config->overlays, list_node) {
		struct device_tree *overlay = unpack_fdt(overlay_chain->image);
		if (!overlay || dt_apply_overlay(dt, overlay)) {
			printk(BIOS_ERR, "Failed to apply overlay %s!\n",
			       overlay_chain->image->name);
		}
	}

	dt_apply_fixups(dt);

	/* Insert coreboot specific information */
	add_cb_fdt_data(dt);
	if (CONFIG(HANDOFF_UPL_DEVICETREE) && config->firmware) {
		upl_fdt_add_reserved_memory(dt, "upl-fit", (uintptr_t)upl_fit,
					    ALIGN_UP(fit_size, 4 * KiB), "boot-data");
		upl_fdt_add_payload(dt, (uintptr_t)upl_fit);
	}

	if (!CONFIG(HANDOFF_UPL_DEVICETREE) || !config->firmware)
		fit_update_memory(dt);

	/* Update device_tree */
#if defined(CONFIG_LINUX_COMMAND_LINE)
	fit_update_chosen(dt, (char *)CONFIG_LINUX_COMMAND_LINE);
#endif
	/* Reserve space for linux-initrd nodes */
	if (config->ramdisk)
		fit_add_ramdisk(dt, 0, 0);

	timestamp_add_now(TS_KERNEL_DECOMPRESSION);

	int status = -1;
	if (config->firmware)
		status = fit_extract_firmware(payload, config, dt, &code, &fdt);
	else if (config->kernel)
		status = fit_extract_kernel(payload, config, dt, &code, &fdt, &initrd);
	if (status != 0)
		return;

	/* Payload allocations must not remain advertised as free UPL memory. */
	if (CONFIG(HANDOFF_UPL_DEVICETREE) && config->firmware)
		upl_fdt_refresh_memory(dt);

	/* Repack FDT for handoff to entrypoint */
	if (!pack_fdt(&fdt, dt)) {
		prog_set_entry(payload, NULL, NULL);
		return;
	}

	timestamp_add_now(TS_KERNEL_START);
}
