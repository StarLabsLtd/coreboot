/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <bootstate.h>
#include <cbmem.h>
#include <common/fsp_params.h>
#include <common/pin_mux.h>
#include <commonlib/bsd/compiler.h>
#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <device/pci.h>
#include <device/pci_def.h>
#include <device/resource.h>
#include <intelblocks/systemagent.h>
#include <intelblocks/vtd.h>
#include <option.h>
#include <soc/pci_devs.h>
#include <soc/ramstage.h>
#include <string.h>

#define CB_PCI_ROOT_BRIDGES_REVISION 1
#define CB_PCI_ROOT_BRIDGE_SUPPORTS  0x7001f
#define CB_PNP0A03_HID               0x0a0341d0

struct cb_pci_handoff_header {
	uint8_t revision;
	uint8_t reserved;
	uint16_t length;
} __packed;

struct cb_pci_aperture {
	uint64_t base;
	uint64_t limit;
	uint64_t translation;
} __packed;

struct cb_pci_root_bridge {
	uint32_t segment;
	uint64_t supports;
	uint64_t attributes;
	bool dma_above_4g;
	bool no_extended_config_space;
	uint64_t allocation_attributes;
	struct cb_pci_aperture bus;
	struct cb_pci_aperture io;
	struct cb_pci_aperture mem;
	struct cb_pci_aperture mem_above_4g;
	struct cb_pci_aperture pmem;
	struct cb_pci_aperture pmem_above_4g;
	uint32_t hid;
	uint32_t uid;
} __packed;

struct cb_pci_root_bridge_info {
	struct cb_pci_handoff_header header;
	bool resource_assigned;
	uint8_t count;
	struct cb_pci_root_bridge root_bridge;
} __packed;

_Static_assert(sizeof(struct cb_pci_handoff_header) == 4,
	       "cbtables PCI handoff header layout changed");
_Static_assert(sizeof(struct cb_pci_root_bridge_info) == 188,
	       "cbtables PCI root bridge layout changed");

static void invalidate_pci_aperture(struct cb_pci_aperture *aperture)
{
	aperture->base = UINT64_MAX;
	aperture->limit = 0;
}

static void include_pci_aperture(struct cb_pci_aperture *aperture, resource_t base,
				 resource_t limit)
{
	if (base > limit)
		return;

	if (aperture->base > aperture->limit) {
		aperture->base = base;
		aperture->limit = limit;
		return;
	}

	aperture->base = MIN(aperture->base, base);
	aperture->limit = MAX(aperture->limit, limit);
}

static uint64_t get_touud(void)
{
	const struct device *root;
	uint64_t touud;

	root = pcidev_path_on_root(SA_DEVFN_ROOT);
	if (!root)
		return 0;

	touud = pci_read_config32(root, TOUUD + sizeof(uint32_t));
	touud <<= 32;
	touud |= pci_read_config32(root, TOUUD);

	return ALIGN_DOWN(touud, MiB);
}

static void export_pci_root_bridge_info(void *unused)
{
	const struct cbmem_entry *entry;
	struct device *domain;
	const struct resource *resource;
	struct cb_pci_root_bridge_info *info;
	struct cb_pci_root_bridge *root_bridge;
	uint64_t tolud;
	uint64_t touud;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION) || !get_uint_option("vtd", 1))
		return;

	domain = dev_find_path(NULL, DEVICE_PATH_DOMAIN);
	if (!domain || !domain->downstream || dev_find_path(domain, DEVICE_PATH_DOMAIN)) {
		printk(BIOS_ERR, "Boot-key PCI handoff requires exactly one domain\n");
		return;
	}

	if (domain->downstream->segment_group != 0 || domain->downstream->secondary != 0) {
		printk(BIOS_ERR, "Boot-key PCI bus aperture is invalid\n");
		return;
	}

	tolud = sa_get_tolud_base();
	touud = get_touud();
	if (!tolud || touud < 4ULL * GiB) {
		printk(BIOS_ERR, "Boot-key PCI DRAM limits are invalid\n");
		return;
	}

	entry = cbmem_entry_add(CBMEM_ID_RB_INFO, sizeof(*info));
	if (!entry || cbmem_entry_size(entry) != sizeof(*info)) {
		printk(BIOS_ERR, "Boot-key PCI handoff allocation failed\n");
		return;
	}

	info = cbmem_entry_start(entry);
	memset(info, 0, sizeof(*info));
	info->header.revision = CB_PCI_ROOT_BRIDGES_REVISION;
	info->header.length = sizeof(*info);
	info->resource_assigned = true;
	info->count = 1;

	root_bridge = &info->root_bridge;
	root_bridge->segment = 0;
	root_bridge->supports = CB_PCI_ROOT_BRIDGE_SUPPORTS;
	root_bridge->attributes = CB_PCI_ROOT_BRIDGE_SUPPORTS;
	root_bridge->bus.base = 0;
	root_bridge->bus.limit = UINT8_MAX;
	root_bridge->hid = CB_PNP0A03_HID;

	invalidate_pci_aperture(&root_bridge->io);
	invalidate_pci_aperture(&root_bridge->mem);
	invalidate_pci_aperture(&root_bridge->mem_above_4g);
	invalidate_pci_aperture(&root_bridge->pmem);
	invalidate_pci_aperture(&root_bridge->pmem_above_4g);

	for (resource = domain->resource_list; resource; resource = resource->next) {
		resource_t base;
		resource_t limit;

		if (!(resource->flags & IORESOURCE_ASSIGNED) ||
		    (resource->flags & IORESOURCE_FIXED) || resource->base > resource->limit)
			continue;

		if (resource->flags & IORESOURCE_IO) {
			include_pci_aperture(&root_bridge->io, resource->base, resource->limit);
			continue;
		}

		if (!(resource->flags & IORESOURCE_MEM))
			continue;

		if (resource->base < 4ULL * GiB) {
			base = MAX(resource->base, tolud);
			limit = MIN(resource->limit, 4ULL * GiB - 1);
			include_pci_aperture(&root_bridge->mem, base, limit);
		}

		if (resource->limit >= 4ULL * GiB) {
			base = MAX(MAX(resource->base, 4ULL * GiB), touud);
			include_pci_aperture(&root_bridge->mem_above_4g, base, resource->limit);
		}
	}

	if (root_bridge->io.base > root_bridge->io.limit ||
	    root_bridge->mem.base > root_bridge->mem.limit ||
	    root_bridge->mem_above_4g.base > root_bridge->mem_above_4g.limit) {
		printk(BIOS_ERR, "Boot-key PCI handoff has missing apertures\n");
		info->count = 0;
		return;
	}

	printk(BIOS_INFO,
	       "Boot-key PCI root: bus %02llx-%02llx, IO %llx-%llx, "
	       "MEM %llx-%llx, MEM64 %llx-%llx\n",
	       root_bridge->bus.base, root_bridge->bus.limit, root_bridge->io.base,
	       root_bridge->io.limit, root_bridge->mem.base, root_bridge->mem.limit,
	       root_bridge->mem_above_4g.base, root_bridge->mem_above_4g.limit);
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_ENTRY, export_pci_root_bridge_info, NULL);

static void quiesce_pci_dma(void *unused)
{
	const struct device *dev;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION) || !get_uint_option("vtd", 1))
		return;

	for (dev = all_devices; dev; dev = dev->next) {
		if (dev->path.type != DEVICE_PATH_PCI || !is_dev_enabled(dev))
			continue;

		pci_dev_disable_bus_master(dev);
	}
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, quiesce_pci_dma, NULL);

void lb_board(struct lb_header *header)
{
	struct lb_range *dma;
	size_t dma_size;
	void *dma_buffer;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION) || !get_uint_option("vtd", 1))
		return;

	dma_buffer = vtd_get_dma_buffer(&dma_size);
	if (!dma_buffer || !dma_size || dma_size > UINT32_MAX) {
		printk(BIOS_ERR, "DMA protection buffer is unavailable\n");
		return;
	}

	dma = (struct lb_range *)lb_new_record(header);
	dma->tag = LB_TAG_DMA;
	dma->size = sizeof(*dma);
	dma->range_start = (uintptr_t)dma_buffer;
	dma->range_size = dma_size;
}

void mainboard_silicon_init_params(FSP_S_CONFIG *supd)
{
	configure_pin_mux(supd);
	starlabs_update_fsp_s_policy(supd);
	supd->TcNotifyIgd = 2; // Auto

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION) || !get_uint_option("vtd", 1))
		return;

	/* Make ACS visible on every PCH root port that FSP exposes. */
	for (size_t i = 0; i < ARRAY_SIZE(supd->PcieRpAcsEnabled); i++)
		supd->PcieRpAcsEnabled[i] = 1;

	/* Keep every USB4 PCIe ingress visible for payload ACS validation. */
	supd->ITbtPcieTunnelingForUsb4 = 1;
	for (size_t i = 0; i < ARRAY_SIZE(supd->ITbtPcieRootPortEn); i++)
		supd->ITbtPcieRootPortEn[i] = 1;
}
