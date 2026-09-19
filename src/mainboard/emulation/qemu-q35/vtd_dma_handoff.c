/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <bootstate.h>
#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/cache.h>
#include <delay.h>
#include <device/device.h>
#include <device/mmio.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <device/resource.h>
#include <stdint.h>
#include <string.h>

#include "q35_dma_policy.h"
#include "vtd_registers.h"

#define Q35_VTD_BASE 0xfed90000U
#define Q35_DMA_PAGE_SIZE 4096U
#define Q35_DMA_TABLE_PAGES 6U
#define Q35_DMA_BLOB_BYTES \
	(sizeof(struct dma_handoff_header) + sizeof(struct dma_handoff_requester) + \
	 3U * sizeof(struct dma_handoff_table))
#define Q35_DMA_TABLE_BYTES (Q35_DMA_TABLE_PAGES * Q35_DMA_PAGE_SIZE)
#define EDU_VENDOR 0x1234U
#define EDU_DEVICE 0x11e8U
#define EDU_DMA_DESTINATION 0x88U
#define EDU_DMA_COUNT 0x90U
#define EDU_DMA_COMMAND 0x98U
#define EDU_DMA_FROM_DEVICE 3U
#define EDU_TEST_BYTES 64U
#define VTD_PAGE_READ_WRITE 3ULL
#define VTD_CONTEXT_PRESENT 1ULL
#define VTD_CONTEXT_AW_48BIT 2ULL
#define VTD_CONTEXT_DOMAIN_ONE (1ULL << 8)
#define VTD_CAP_SAGAW_48BIT (1ULL << 10)
#define VTD_ECAP_PAGE_WALK_COHERENT (1ULL << 0)

static uint8_t *handoff_allocation;
static uint8_t *table_allocation;
static struct device *edu_requester;
static uint16_t requester_bdf;
static bool backend_ready;
static bool tables_committed;
static bool noncoherent_writeback;
static uint8_t dma_target[Q35_DMA_PAGE_SIZE] __aligned(Q35_DMA_PAGE_SIZE);

static uint32_t vtd_read32(void *context, uint32_t offset)
{
	return read32((uint8_t *)context + offset);
}

static void vtd_write32(void *context, uint32_t offset, uint32_t value)
{
	write32((uint8_t *)context + offset, value);
}

static uint64_t vtd_read64(uint32_t offset)
{
	uint64_t low = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, offset);
	uint64_t high = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, offset + 4U);

	return low | (high << 32);
}

static uint64_t *table_page(size_t page)
{
	return (void *)(table_allocation + page * Q35_DMA_PAGE_SIZE);
}

static void populate_requester_hierarchy(void)
{
	uint64_t *root = table_page(0);
	uint64_t *context = table_page(1);
	uint64_t *pml4 = table_page(2);
	uint64_t *pdpt = table_page(3);
	uint64_t *pd = table_page(4);
	const uint64_t target = (uintptr_t)dma_target;

	root[(requester_bdf >> 8) * 2U] = (uintptr_t)context | VTD_CONTEXT_PRESENT;
	context[(requester_bdf & 0xffU) * 2U] =
		(uintptr_t)pml4 | VTD_CONTEXT_PRESENT;
	context[(requester_bdf & 0xffU) * 2U + 1U] =
		VTD_CONTEXT_AW_48BIT | VTD_CONTEXT_DOMAIN_ONE;
	pml4[(target >> 39) & 0x1ffU] = (uintptr_t)pdpt | VTD_PAGE_READ_WRITE;
	pdpt[(target >> 30) & 0x1ffU] = (uintptr_t)pd | VTD_PAGE_READ_WRITE;
	pd[(target >> 21) & 0x1ffU] =
		(uintptr_t)table_page(5) | VTD_PAGE_READ_WRITE;
}

static bool table_pages_unchanged(void)
{
	const uint64_t target = (uintptr_t)dma_target;

	if (!table_allocation)
		return false;
	for (size_t page = 0; page < Q35_DMA_TABLE_PAGES; page++) {
		const uint64_t *entries = table_page(page);

		for (size_t slot = 0; slot < Q35_DMA_PAGE_SIZE / sizeof(*entries); slot++) {
			uint64_t expected = 0;

			if (page == 0 && slot == (requester_bdf >> 8) * 2U)
				expected = (uintptr_t)table_page(1) | VTD_CONTEXT_PRESENT;
			else if (page == 1 && slot == (requester_bdf & 0xffU) * 2U)
				expected = (uintptr_t)table_page(2) | VTD_CONTEXT_PRESENT;
			else if (page == 1 && slot == (requester_bdf & 0xffU) * 2U + 1U)
				expected = VTD_CONTEXT_AW_48BIT | VTD_CONTEXT_DOMAIN_ONE;
			else if (page == 2 && slot == ((target >> 39) & 0x1ffU))
				expected = (uintptr_t)table_page(3) | VTD_PAGE_READ_WRITE;
			else if (page == 3 && slot == ((target >> 30) & 0x1ffU))
				expected = (uintptr_t)table_page(4) | VTD_PAGE_READ_WRITE;
			else if (page == 4 && slot == ((target >> 21) & 0x1ffU))
				expected = (uintptr_t)table_page(5) | VTD_PAGE_READ_WRITE;
			if (entries[slot] != expected)
				return false;
		}
	}
	return true;
}

static void commit_vtd_tables(void *unused)
{
	(void)unused;
	if (!(vtd_read64(Q35_VTD_ECAP) & VTD_ECAP_PAGE_WALK_COHERENT)) {
		if (clflush_supported())
			clflush_region((uintptr_t)table_page(0), Q35_DMA_TABLE_BYTES);
		else
			wbinvd();
		noncoherent_writeback = true;
	}
	asm volatile("mfence" ::: "memory");
	tables_committed = true;
}

static bool requester_bus_master_clear(void)
{
	return edu_requester &&
		!(pci_read_config16(edu_requester, PCI_COMMAND) & PCI_COMMAND_MASTER);
}

static void prove_requester_denied(void)
{
	struct resource *bar = probe_resource(edu_requester, PCI_BASE_ADDRESS_0);
	uint8_t *mmio;
	uint64_t cap;
	uint64_t fault_high;
	uint32_t fault_offset;
	uint16_t command;

	if (!bar || (bar->flags & (IORESOURCE_MEM | IORESOURCE_ASSIGNED)) !=
	    (IORESOURCE_MEM | IORESOURCE_ASSIGNED) || bar->size < 0x100000 ||
	    (uintptr_t)dma_target > UINT32_MAX)
		die("Q35 DMA: EDU denial probe resources absent");
	mmio = res2mmio(bar, 0, 0);
	memset(dma_target, 0x5a, EDU_TEST_BYTES);
	command = pci_read_config16(edu_requester, PCI_COMMAND);
	pci_write_config16(edu_requester, PCI_COMMAND,
		command | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	if (!(pci_read_config16(edu_requester, PCI_COMMAND) & PCI_COMMAND_MASTER))
		die("Q35 DMA: EDU denial probe could not assert BME");
	write64(mmio + EDU_DMA_DESTINATION, (uintptr_t)dma_target);
	write64(mmio + EDU_DMA_COUNT, EDU_TEST_BYTES);
	write64(mmio + EDU_DMA_COMMAND, EDU_DMA_FROM_DEVICE);
	mdelay(200);
	pci_write_config16(edu_requester, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
	if (!requester_bus_master_clear())
		die("Q35 DMA: EDU denial probe left BME active");

	cap = vtd_read64(Q35_VTD_CAP);
	fault_offset = (uint32_t)((cap >> 24) & 0x3ffU) * 16U;
	if (fault_offset < 0x40U || fault_offset > 0xff0U)
		die("Q35 DMA: invalid fault-record offset");
	fault_high = vtd_read64(fault_offset + 8U);
	if (!(vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_FSTS) &
	      Q35_VTD_FAULT_PENDING) || !(fault_high & (1ULL << 63)) ||
	    (fault_high & 0xffffU) != requester_bdf ||
	    ((fault_high >> 32) & 0xffU) != 5U)
		die("Q35 DMA: EDU DMA did not fault in domain 1's empty leaf");
	for (size_t byte = 0; byte < EDU_TEST_BYTES; byte++)
		if (dma_target[byte] != 0x5a)
			die("Q35 DMA: denied EDU DMA changed the target");
	write32((void *)(uintptr_t)(Q35_VTD_BASE + fault_offset + 12U), 1U << 31);
	if (vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_FSTS) &
	    Q35_VTD_FAULT_PENDING)
		die("Q35 DMA: EDU denial fault did not clear");
	printk(BIOS_INFO,
	       "Q35 DMA: denied EDU %#x in domain 1, reason %#llx, target unchanged, BME clear\n",
	       requester_bdf, (unsigned long long)((fault_high >> 32) & 0xffU));
}

static void q35_dma_backend_enable(void *unused)
{
	const struct q35_vtd_io io = {
		.context = (void *)(uintptr_t)Q35_VTD_BASE,
		.read32 = vtd_read32,
		.write32 = vtd_write32,
		.commit_tables = commit_vtd_tables,
	};
	struct device *duplicate;
	const struct cbmem_entry *blob_entry;
	const struct cbmem_entry *table_entry;
	uint8_t *root;
	int result;

	(void)unused;
	edu_requester = dev_find_device(EDU_VENDOR, EDU_DEVICE, NULL);
	duplicate = edu_requester ?
		dev_find_device(EDU_VENDOR, EDU_DEVICE, edu_requester) : NULL;
	if (!edu_requester || duplicate || !edu_requester->upstream ||
	    edu_requester->upstream->segment_group != 0 ||
	    edu_requester->upstream->secondary > UINT8_MAX ||
	    edu_requester->path.pci.devfn > UINT8_MAX)
		die("Q35 DMA: expected exactly one segment-zero EDU requester");

	requester_bdf = ((uint16_t)edu_requester->upstream->secondary << 8) |
		(uint16_t)edu_requester->path.pci.devfn;
	pci_write_config16(edu_requester, PCI_COMMAND,
		pci_read_config16(edu_requester, PCI_COMMAND) & ~PCI_COMMAND_MASTER);
	if (!requester_bus_master_clear())
		die("Q35 DMA: requester BME could not be cleared");

	blob_entry = cbmem_entry_add(CBMEM_ID_DMA_HANDOFF, Q35_DMA_BLOB_BYTES);
	if (!blob_entry || cbmem_entry_size(blob_entry) != Q35_DMA_BLOB_BYTES)
		die("Q35 DMA: modern handoff allocation size is not exact");
	handoff_allocation = cbmem_entry_start(blob_entry);
	table_entry = cbmem_entry_add(CBMEM_ID_Q35_VTD_TABLES, Q35_DMA_TABLE_BYTES);
	if (!table_entry || cbmem_entry_size(table_entry) != Q35_DMA_TABLE_BYTES)
		die("Q35 DMA: resident table allocation size is not exact");
	table_allocation = cbmem_entry_start(table_entry);
	if (!handoff_allocation || !table_allocation ||
	    ((uintptr_t)table_allocation & (Q35_DMA_PAGE_SIZE - 1U)))
		die("Q35 DMA: aligned split allocations failed");
	memset(handoff_allocation, 0, Q35_DMA_BLOB_BYTES);
	memset(table_allocation, 0, Q35_DMA_TABLE_BYTES);
	root = table_allocation;
	if ((uintptr_t)root > UINT32_MAX - Q35_DMA_TABLE_BYTES)
		die("Q35 DMA: QEMU legacy tables are not wholly below 4 GiB");
	if (!(vtd_read64(Q35_VTD_CAP) & VTD_CAP_SAGAW_48BIT))
		die("Q35 DMA: VT-d lacks the required 48-bit address width");
	populate_requester_hierarchy();
	if (!table_pages_unchanged())
		die("Q35 DMA: requester deny hierarchy is malformed");

	result = q35_vtd_default_deny(&io, (uint32_t)(uintptr_t)root);
	if (result)
		die("Q35 DMA: default-deny enable failed %d", result);
	if (!tables_committed)
		die("Q35 DMA: VT-d tables were not committed before enable");
	printk(BIOS_INFO, "Q35 DMA: %s page-walk table visibility established\n",
	       noncoherent_writeback ? "noncoherent" : "coherent");
	prove_requester_denied();
	if (!(vtd_read32(io.context, Q35_VTD_GSTS) &
	      Q35_VTD_TRANSLATION_ENABLE) || !table_pages_unchanged() ||
	    !requester_bus_master_clear())
		die("Q35 DMA: protected state did not survive read-back");
	backend_ready = true;
	printk(BIOS_INFO,
	       "Q35 DMA: default-deny active, EDU %04x:%02x:%02x.%x BME clear\n",
	       0, requester_bdf >> 8, (requester_bdf & 0xffU) >> 3,
	       requester_bdf & 7U);
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, q35_dma_backend_enable, NULL);

bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	struct dma_handoff_requester requester;
	struct dma_handoff_table tables[3];
	const uint64_t generation = payload_resource_revision4_generation();
	const uint16_t requesters[] = { requester_bdf };
	const struct q35_dma_facts facts = {
		.generation = generation,
		.resource_generation = payload_resource_revision4_generation(),
		.requesters = requesters,
		.requester_count = ARRAY_SIZE(requesters),
		.translation_active = !!(vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
			Q35_VTD_GSTS) & Q35_VTD_TRANSLATION_ENABLE),
		.tables_resident = handoff_allocation != NULL,
		.tables_unchanged = table_pages_unchanged(),
		.bus_master_clear = requester_bus_master_clear(),
	};
	uint8_t *table_base;
	size_t written;

	if (!address || !bytes || !backend_ready ||
	    !payload_resource_revision4_published() || !q35_dma_facts_valid(&facts))
		return false;
	table_base = table_allocation;
	memset(&requester, 0, sizeof(requester));
	requester.bdf = requester_bdf;
	requester.domain = 1;
	requester.flags = DMA_HANDOFF_REQUESTER_FLAGS;
	requester.root_table = 0;
	requester.context_table = 1;
	requester.hierarchy_table = 2;
	requester.generation = generation;
	memset(tables, 0, sizeof(tables));
	tables[0].base = (uintptr_t)table_base;
	tables[0].pages = 1;
	tables[0].owner_type = DMA_HANDOFF_TABLE_GLOBAL;
	tables[0].flags = DMA_HANDOFF_TABLE_FLAGS;
	tables[1].base = (uintptr_t)(table_base + Q35_DMA_PAGE_SIZE);
	tables[1].pages = 1;
	tables[1].owner_type = DMA_HANDOFF_TABLE_BUS;
	tables[1].owner_id = requester_bdf >> 8;
	tables[1].flags = DMA_HANDOFF_TABLE_FLAGS;
	tables[2].base = (uintptr_t)(table_base + 2U * Q35_DMA_PAGE_SIZE);
	tables[2].pages = 4;
	tables[2].owner_type = DMA_HANDOFF_TABLE_REQUESTER;
	tables[2].owner_id = requester_bdf;
	tables[2].flags = DMA_HANDOFF_TABLE_FLAGS;
	if (dma_handoff_build(handoff_allocation, Q35_DMA_BLOB_BYTES, generation,
		&requester, 1, tables, ARRAY_SIZE(tables), &written) != CB_SUCCESS)
		return false;
	*address = (uintptr_t)handoff_allocation;
	*bytes = written;
	printk(BIOS_INFO,
	       "Q35 DMA: handoff generation %llu, domain 1 linked, six immutable pages\n",
	       (unsigned long long)generation);
	return true;
}
