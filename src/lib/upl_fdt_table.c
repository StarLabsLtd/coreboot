/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boardid.h>
#include <bootmode.h>
#include <boot/upl_fdt_table.h>
#include <bootmem.h>
#include <cbfs.h>
#include <cbmem.h>
#include <commonlib/device_tree.h>
#include <console/console.h>
#include <console/uart.h>
#include <cpu/cpu.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <fit.h>
#include <stdio.h>
#include <stdlib.h>
#include <version.h>

__weak uint32_t board_id(void) { return UNDEFINED_STRAPPING_ID; }
__weak uint32_t ram_code(void) { return UNDEFINED_STRAPPING_ID; }
__weak uint32_t sku_id(void) { return UNDEFINED_STRAPPING_ID; }

__weak const char *upl_fdt_add_serial(struct device_tree_node *node) { return ""; }

static int write_options_node(struct device_tree *tree)
{
	struct device_tree_node *options_node;
	options_node = dt_find_node_by_path(tree, "/options", NULL, NULL, 1);
	if (!options_node) {
		printk(BIOS_ERR, "%s: options node is null", __func__);
		return -1;
	}

	// Add some UPL specific parameters
	struct device_tree_node *params_node;
	params_node = dt_find_node_by_path(tree, "/options/upl-params", NULL, NULL, 1);
	if (!params_node) {
		printk(BIOS_ERR, "%s: upl-params node is null", __func__);
		return -1;
	}
	const char *boot_mode = get_boot_mode() == LB_BOOT_MODE_FLASH_UPDATE ?
		"flash-update" : "normal";

	dt_add_string_prop(params_node, "compatible", "upl");
	dt_add_u32_prop(params_node, "addr-width", cpu_phys_address_size());
	dt_add_bin_prop(params_node, "pci-enum-done", NULL, 0);
	dt_add_string_prop(params_node, "boot-mode", boot_mode);

	// Add coreboot node, which contains information about coreboot like version and name
	struct device_tree_node *cb_node;
	cb_node = dt_find_node_by_path(tree, "/options/upl-custom/coreboot", NULL, NULL, 1);
	if (!cb_node) {
		printk(BIOS_ERR, "%s: upl-custom/coreboot node is null", __func__);
		return -1;
	}
	dt_add_string_prop(cb_node, "compatible", "upl-coreboot");
	dt_add_string_prop(cb_node, "name", "coreboot");
	dt_add_string_prop(cb_node, "version", coreboot_version);

	return 0;
}

void upl_fdt_add_payload(struct device_tree *tree, uintptr_t fit_address)
{
	char node_name[64];
	int status = snprintf(node_name, sizeof(node_name), "/options/upl-image@%lx",
			      fit_address);
	assert(status >= 0 && status < (int)sizeof(node_name));

	struct device_tree_node *image_node =
		dt_find_node_by_path(tree, node_name, NULL, NULL, 1);
	if (!image_node) {
		printk(BIOS_ERR, "%s: %s node is null\n", __func__, node_name);
		return;
	}

	/* The preserved FIT contains the FV data-offset targets used by EDK2. */
	dt_add_u64_prop(image_node, "addr", fit_address);
}

void upl_fdt_add_reserved_memory(struct device_tree *tree, const char *name,
				 uintptr_t address, size_t size, const char *type)
{
	char node_name[96];
	u32 addr_cells = 2;
	u32 size_cells = 2;
	u64 range_address = address;
	u64 range_size = size;
	int status;

	dt_read_cell_props(tree->root, &addr_cells, &size_cells);
	status = snprintf(node_name, sizeof(node_name), "/reserved-memory/%s@%lx",
			  name, address);
	assert(status >= 0 && status < (int)sizeof(node_name));

	struct device_tree_node *node =
		dt_find_node_by_path(tree, node_name, NULL, NULL, 1);
	if (!node) {
		printk(BIOS_ERR, "%s: %s node is null\n", __func__, node_name);
		return;
	}

	dt_add_bin_prop(node, "no-map", NULL, 0);
	dt_add_reg_prop(node, &range_address, &range_size, 1, addr_cells,
			size_cells);
	if (type)
		dt_add_string_prop(node, "compatible", type);
}

#define NON_RELOCATABLE	BIT(31)
#define IO_SPACE	BIT(24)
#define MMIO_SPACE	BIT(25)
#define MMIO64_SPACE	(BIT(24) | BIT(25))

static void write_pci_rb_range(uint32_t *ranges, uint32_t *index,
			       uint32_t memory_space, u64 base, u64 limit)
{
	ranges[(*index)++] = cpu_to_be32(NON_RELOCATABLE | memory_space);

	uint32_t child_addr_index = *index;
	ranges[(*index)++] = cpu_to_be32(base >> 32);
	ranges[(*index)++] = cpu_to_be32(base & 0xffffffff);

	ranges[(*index)++] = ranges[child_addr_index];
	ranges[(*index)++] = ranges[child_addr_index + 1];

	u64 size = limit - base + 1;
	ranges[(*index)++] = cpu_to_be32(size >> 32);
	ranges[(*index)++] = cpu_to_be32(size & 0xffffffff);
}

static int write_pci_rb_node(struct device_tree *tree)
{
	uint64_t addr = CONFIG_ECAM_MMCONF_BASE_ADDRESS;
	uint64_t size = CONFIG_ECAM_MMCONF_LENGTH;
	char node_name[64];
	int status;
	struct device_tree_node *pci_rb_node;
	uint32_t *ranges;
	uint32_t ranges_index = 0;

	/* PCI Bus Binding requires three address cells and two size cells. */
	u32 pci_addr_cells = 3, pci_size_cells = 2;
	u32 host_addr_cells = 2, host_size_cells = 2;

	status = snprintf(node_name, sizeof(node_name), "/pci-rb0@%llx", addr);
	assert(status >= 0 && status < (int)sizeof(node_name));
	pci_rb_node = dt_find_node_by_path(tree, node_name, NULL, NULL, 1);
	if (!pci_rb_node) {
		printk(BIOS_ERR, "%s: %s node is null", __func__, node_name);
		return -1;
	}
	dt_add_u32_prop(pci_rb_node, "#address-cells", pci_addr_cells);
	dt_add_u32_prop(pci_rb_node, "#size-cells", pci_size_cells);
	dt_add_string_prop(pci_rb_node, "compatible", "pci-rb");

	dt_add_reg_prop(pci_rb_node, &addr, &size, 1, host_addr_cells, host_size_cells);

	struct device *domain = dev_find_path(NULL, DEVICE_PATH_DOMAIN);
	if (!domain) {
		printk(BIOS_ERR, "%s: PCI domain is unavailable\n", __func__);
		return -1;
	}

	ranges = malloc(21 * sizeof(*ranges));
	if (!ranges) {
		printk(BIOS_ERR, "%s: PCI ranges allocation failed\n", __func__);
		return -1;
	}

	const struct resource *res;
	for (res = domain->resource_list; res; res = res->next) {
		if (!(res->flags & IORESOURCE_SUBTRACTIVE) || res->base > res->limit)
			continue;
		if (ranges_index + 7 > 21) {
			printk(BIOS_ERR, "%s: too many PCI domain apertures\n", __func__);
			return -1;
		}

		if (res->flags & IORESOURCE_IO)
			write_pci_rb_range(ranges, &ranges_index, IO_SPACE,
					   res->base, res->limit);
		else if ((res->flags & IORESOURCE_MEM) && res->base < 4ULL * GiB) {
			u64 base = res->base;
			u64 limit = res->limit;
			u64 top_of_low_dram = bootmem_top_of_low_dram();

			base = MAX(base, top_of_low_dram);
			if (base > limit) {
				printk(BIOS_ERR, "%s: PCI MMIO aperture is empty\n", __func__);
				return -1;
			}

			write_pci_rb_range(ranges, &ranges_index, MMIO_SPACE,
						   base, limit);
		} else if (res->flags & IORESOURCE_MEM) {
			u64 base = MAX(res->base, bootmem_top_of_dram());

			if (base > res->limit) {
				printk(BIOS_ERR, "%s: PCI MMIO64 aperture is empty\n", __func__);
				return -1;
			}
			write_pci_rb_range(ranges, &ranges_index, MMIO64_SPACE,
					   base, res->limit);
		}
	}

	if (!ranges_index) {
		printk(BIOS_ERR, "%s: PCI domain has no allocatable apertures\n", __func__);
		return -1;
	}

	dt_add_bin_prop(pci_rb_node, "ranges", ranges,
			ranges_index * sizeof(ranges[0]));

	uint32_t *bus_range = malloc(2 * sizeof(*bus_range));
	if (!bus_range) {
		printk(BIOS_ERR, "%s: PCI bus range allocation failed\n", __func__);
		return -1;
	}
	bus_range[0] = cpu_to_be32(0);
	bus_range[1] = cpu_to_be32(MIN(CONFIG_ECAM_MMCONF_BUS_NUMBER - 1, 0xff));
	dt_add_bin_prop(pci_rb_node, "bus-range", bus_range,
			2 * sizeof(*bus_range));

	return 0;
}

static int write_isa_node(struct device_tree *tree)
{
	// #address-cells = 2, #size-cells = 1 is the default according to devicetree spec
	u32 addr_cells = 2, size_cells = 1;

	// creates an isa node that holds devices that are accessed via I/O Ports
	// currently that is only the case for x86 devices (e.g. serial, cmos...)
	struct device_tree_node *isa_node = dt_find_node_by_path(tree, "/isa", NULL, NULL, 1);
	if (!isa_node) {
		printk(BIOS_ERR, "%s: isa node is null", __func__);
		return -1;
	}
	dt_add_u32_prop(isa_node, "#address-cells", addr_cells);
	dt_add_u32_prop(isa_node, "#size-cells", size_cells);
	dt_add_string_prop(isa_node, "compatible", "isa");

	// Add stdout serial
	if (CONFIG(CONSOLE_SERIAL)) {
		const char *serial_path = upl_fdt_add_serial(tree->root);
		if (serial_path) {
			struct device_tree_node *chosen_node;
			chosen_node = dt_find_node_by_path(tree, "/chosen", NULL, NULL, 1);
			if (!chosen_node) {
				printk(BIOS_ERR, "%s: chosen node is null", __func__);
				return -1;
			}
			dt_add_string_prop(chosen_node, "stdout-path", serial_path);
		}
	}

	return 0;
}

static int write_framebuffer_node(struct device_tree *tree)
{
	const struct lb_framebuffer *fb = get_lb_framebuffer();
	const char *format;
	char node_name[32];
	u32 addr_cells = 2, size_cells = 2;

	if (!fb)
		return 0;

	int status = snprintf(node_name, sizeof(node_name), "/framebuffer@%llx",
			      fb->physical_address);
	assert(status >= 0 && status < (int)sizeof(node_name));

	struct device_tree_node *framebuffer_node;
	framebuffer_node = dt_find_node_by_path(tree, node_name, NULL, NULL, 1);
	if (!framebuffer_node) {
		printk(BIOS_ERR, "%s: framebuffer node is null", __func__);
		return -1;
	}

	// "display" property pointing into PCI-RB node is required,
	// but skipping it only means no HOB to describe the graphics device, and that's fine.
	dt_add_string_prop(framebuffer_node, "compatible", "simple-framebuffer");

	if (fb->bits_per_pixel != 32 || fb->red_mask_size != 8 ||
	    fb->green_mask_size != 8 || fb->green_mask_pos != 8 ||
	    fb->blue_mask_size != 8 ||
	    !((fb->reserved_mask_size == 8 && fb->reserved_mask_pos == 24) ||
	      fb->reserved_mask_size == 0)) {
		printk(BIOS_ERR, "%s: unsupported framebuffer format\n", __func__);
		return -1;
	}

	if (fb->red_mask_pos == 16 && fb->blue_mask_pos == 0)
		format = fb->reserved_mask_size == 8 ? "a8r8g8b8" : "x8r8g8b8";
	else if (fb->red_mask_pos == 0 && fb->blue_mask_pos == 16)
		format = fb->reserved_mask_size == 8 ? "a8b8g8r8" : "x8b8g8r8";
	else {
		printk(BIOS_ERR, "%s: unsupported framebuffer channel layout\n", __func__);
		return -1;
	}

	u64 addr = fb->physical_address;
	u64 size = (u64)fb->bytes_per_line * fb->y_resolution;
	dt_read_cell_props(tree->root, &addr_cells, &size_cells);
	dt_add_reg_prop(framebuffer_node, &addr, &size, 1, addr_cells, size_cells);

	dt_add_string_prop(framebuffer_node, "format", format);
	dt_add_u32_prop(framebuffer_node, "height", fb->y_resolution);
	dt_add_u32_prop(framebuffer_node, "stride", fb->bytes_per_line);
	dt_add_u32_prop(framebuffer_node, "width", fb->x_resolution);

	return 0;
}

static int write_reserved_memory_node(struct device_tree *tree)
{
	u32 addr_cells = 2, size_cells = 2;

	char node_name[32];
	u64 addr, size;
	int status;
	struct device_tree_node *node;
	struct device_tree_node *rsvd_node = dt_find_node_by_path(tree, "/reserved-memory", NULL, NULL, 0);
	if (!rsvd_node) {
		printk(BIOS_ERR, "%s: reserved-memory node is null", __func__);
		return -1;
	}
	if (CONFIG(HAVE_ACPI_TABLES)) {
		const struct cbmem_entry *acpi_region = cbmem_entry_find(CBMEM_ID_ACPI);
		if (!acpi_region) {
			printk(BIOS_ERR, "%s: ACPI CBMEM entry is unavailable\n", __func__);
			return -1;
		}
		addr = (unsigned long)cbmem_entry_start(acpi_region);
		size = cbmem_entry_size(acpi_region);

		status = snprintf(node_name, sizeof(node_name), "memory@%llx", addr);
		assert(status >= 0 && status < (int)sizeof(node_name));
		const char *node_names_acpi[] = { node_name, NULL };
		node = dt_find_node(rsvd_node, node_names_acpi, NULL, NULL, 1);
		if (!node) {
			printk(BIOS_ERR, "%s: ACPI node is null", __func__);
			return -1;
		}
		dt_add_string_prop(node, "compatible", "acpi");
		dt_add_reg_prop(node, &addr, &size, 1, addr_cells, size_cells);
	}
	if (CONFIG(GENERATE_SMBIOS_TABLES)) {
		const struct cbmem_entry *smbios_region = cbmem_entry_find(CBMEM_ID_SMBIOS);
		if (!smbios_region) {
			printk(BIOS_ERR, "%s: SMBIOS CBMEM entry is unavailable\n", __func__);
			return -1;
		}
		addr = (unsigned long)cbmem_entry_start(smbios_region);
		size = cbmem_entry_size(smbios_region);

		status = snprintf(node_name, sizeof(node_name), "memory@%llx", addr);
		assert(status >= 0 && status < (int)sizeof(node_name));
		const char *node_names_smbios[] = { node_name, NULL };
		node = dt_find_node(rsvd_node, node_names_smbios, NULL, NULL, 1);
		if (!node) {
			printk(BIOS_ERR, "%s: SMBIOS node is null", __func__);
			return -1;
		}
		dt_add_string_prop(node, "compatible", "smbios");
		dt_add_reg_prop(node, &addr, &size, 1, addr_cells, size_cells);
	}

	return 0;
}

/*
 * Writes a FDT (Flattened Device Tree) compliant to the UPL (Universal Payload Specification)
 * at table_start and advances the pointer by the size of the FDT.
 * This FDT is used as an alternative to the coreboot tables for the handoff to the payload.
 * Some architectures (like RISC-V) already use an FDT for handoff.
 * In that case the UPL specific FDT structues will simply be extended into the exisitng FDT.
 */
uintptr_t write_upl_fdt_table(uintptr_t table_start, size_t table_capacity,
			      bool use_existing_fdt)
{
	/* UPL x86 uses 64-bit address and size cells at the root. */
	u32 addr_cells = 2, size_cells = 2;

	// these structures are only used if there is no existing devicetree in CBMEM
	struct fdt_header header = {
		.magic = cpu_to_be32(FDT_HEADER_MAGIC),
		.version = cpu_to_be32(FDT_SUPPORTED_VERSION),
		.last_comp_version = cpu_to_be32(FDT_SUPPORTED_VERSION),
	};
	struct device_tree_node root = {
		.name = "/",
	};
	struct device_tree dev_tree = {
		.header = &header,
		.header_size = sizeof(header),
		.root = &root,
	};
	// Required to be present
	struct device_tree_reserve_map_entry *entry = xzalloc(sizeof(*entry));
	entry->start = 0;
	entry->size = 0;
	list_insert_after(&entry->list_node, &dev_tree.reserve_map);
	header.reserve_map_offset = cpu_to_be32(ALIGN_UP(dev_tree.header_size, 8));

	struct device_tree *tree;
	void *dtb = (void *)table_start;
	if (use_existing_fdt) {
		printk(BIOS_DEBUG, "%s: Using existing devicetree\n", __func__);
		tree = fdt_unflatten(dtb);
		//free(dtb);
		if (!tree) {
			printk(BIOS_ERR, "%s: error unflattening devicetree\n", __func__);
			return table_start;
		}
	} else {
		printk(BIOS_DEBUG, "%s: Creating new UPL devicetree\n", __func__);
		tree = &dev_tree;
		dt_add_u32_prop(tree->root, "#address-cells", addr_cells);
		dt_add_u32_prop(tree->root, "#size-cells", size_cells);
	}

	if (write_options_node(tree))
		goto error;

	if (write_pci_rb_node(tree))
		goto error;

	if (write_isa_node(tree))
		goto error;

	if (write_framebuffer_node(tree))
		goto error;

	// add memory and memory-reserved nodes
	upl_fdt_add_memory(tree);

	if (write_reserved_memory_node(tree))
		goto error;

	printk(BIOS_DEBUG, "Writing UPL devicetree at 0x%08lx\n", (long)table_start);

	size_t dt_size = dt_flat_size(tree);
	if (dt_size > table_capacity) {
		printk(BIOS_ERR, "%s: UPL FDT is too large (%zx/%zx)\n",
		       __func__, dt_size, table_capacity);
		goto error;
	}
	dt_flatten(tree, (void *)table_start);
	if (use_existing_fdt)
		free(tree);

	return table_start + dt_size;

error:
	if (use_existing_fdt)
		free(tree);
	return table_start;
}
