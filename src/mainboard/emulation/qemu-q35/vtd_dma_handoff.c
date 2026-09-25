/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <boot/capsule_broker_buffers.h>
#include <boot/dma_handoff.h>
#include <bootstate.h>
#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/cache.h>
#include <delay.h>
#include <device/device.h>
#include <device/fw_cfg.h>
#include <device/mmio.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <device/resource.h>
#include <stdint.h>
#include <string.h>

#include "q35_dma_policy.h"
#include "q35_capsule_dma_proof.h"
#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
#include "q35_mor_dma.h"
#include "q35_mor_pci_guard.h"
#endif
#include "vtd_registers.h"

#define Q35_VTD_BASE 0xfed90000U
#define Q35_DMA_PAGE_SIZE 4096U
#define Q35_DMA_REQUESTERS 2U
#define Q35_DMA_TABLE_PAGES 10U
#define Q35_DMA_BLOB_BYTES \
	(sizeof(struct dma_handoff_header) + \
	 Q35_DMA_REQUESTERS * sizeof(struct dma_handoff_requester))
#define Q35_DMA_TABLE_BYTES (Q35_DMA_TABLE_PAGES * Q35_DMA_PAGE_SIZE)
#define Q35_DMA_ARENA_PAGES (32U + 128U)
#define Q35_DMA_ARENA_BYTES \
	((Q35_DMA_ARENA_PAGES + 1U) * Q35_DMA_PAGE_SIZE)
#define Q35_NVME_ARENA_PAGES 32U
#define Q35_XHCI_ARENA_PAGES 128U
#define Q35_NVME_IOVA 0x80000000ULL
#define Q35_XHCI_IOVA 0x90000000ULL
#define Q35_NVME_BDF 0x0018U
#define Q35_XHCI_BDF 0x0020U
#define Q35_EDU_BDF 0x0028U
#define QEMU_VENDOR 0x1b36U
#define QEMU_NVME_DEVICE 0x0010U
#define QEMU_XHCI_DEVICE 0x000dU
#define EDU_VENDOR 0x1234U
#define EDU_DEVICE 0x11e8U
#define EDU_DMA_DESTINATION 0x88U
#define EDU_DMA_COUNT 0x90U
#define EDU_DMA_COMMAND 0x98U
#define EDU_DMA_FROM_DEVICE 3U
#define EDU_TEST_BYTES 64U
#define Q35_DMA_MAX_PCI_FUNCTIONS 64U
#define VTD_PAGE_READ_WRITE 3ULL
#define VTD_CONTEXT_PRESENT 1ULL
#define VTD_CONTEXT_AW_48BIT 2ULL
#define VTD_CAP_SAGAW_48BIT (1ULL << 10)
#define VTD_ECAP_PAGE_WALK_COHERENT (1ULL << 0)

static uint8_t *handoff_allocation;
static uint8_t *table_allocation;
static uint8_t *arena_allocation;
static struct device *edu_requester;
static struct device *dma_devices[Q35_DMA_REQUESTERS];
static uint16_t requester_bdfs[Q35_DMA_REQUESTERS];
static uint16_t requester_domains[Q35_DMA_REQUESTERS] = { 1U, 2U };
static uint8_t *requester_arenas[Q35_DMA_REQUESTERS];
static const uint32_t requester_arena_pages[Q35_DMA_REQUESTERS] = {
	Q35_NVME_ARENA_PAGES, Q35_XHCI_ARENA_PAGES
};
static const uint64_t requester_iovas[Q35_DMA_REQUESTERS] = {
	Q35_NVME_IOVA, Q35_XHCI_IOVA
};
static bool backend_ready;
static bool tables_committed;
static bool noncoherent_writeback;
static uint8_t dma_target[Q35_DMA_PAGE_SIZE] __aligned(Q35_DMA_PAGE_SIZE);
static uint32_t pci_requesters[Q35_DMA_MAX_PCI_FUNCTIONS];
static size_t pci_requester_count;
static struct q35_dma_pmr_state pmr_snapshot;
static struct q35_capsule_dma_geometry capsule_geometry;
#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
enum q35_mor_dma_phase {
	Q35_MOR_DMA_EMPTY,
	Q35_MOR_DMA_EARLY_DENY,
	Q35_MOR_DMA_FINAL_ACTIVE,
	Q35_MOR_DMA_FAILED,
};

static uint64_t mor_early_root[Q35_DMA_PAGE_SIZE / sizeof(uint64_t)]
	__aligned(Q35_DMA_PAGE_SIZE);
static struct q35_mor_pci_requester mor_pci_requesters[Q35_DMA_MAX_PCI_FUNCTIONS];
static size_t mor_pci_requester_count;
static uint8_t mor_dma_phase;
static bool mor_early_tables_committed;
#endif

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

static size_t hierarchy_page(size_t requester, size_t level)
{
	return 2U + requester * 4U + level;
}

static void populate_requester_hierarchy(size_t requester)
{
	uint64_t *root = table_page(0);
	uint64_t *context = table_page(1);
	uint64_t *pml4 = table_page(hierarchy_page(requester, 0));
	uint64_t *pdpt = table_page(hierarchy_page(requester, 1));
	uint64_t *pd = table_page(hierarchy_page(requester, 2));
	uint64_t *pt = table_page(hierarchy_page(requester, 3));
	const uint64_t iova = requester_iovas[requester];
	const uint64_t arena = (uintptr_t)requester_arenas[requester];
	const uint16_t bdf = requester_bdfs[requester];

	root[(bdf >> 8) * 2U] = (uintptr_t)context | VTD_CONTEXT_PRESENT;
	context[(bdf & 0xffU) * 2U] =
		(uintptr_t)pml4 | VTD_CONTEXT_PRESENT;
	context[(bdf & 0xffU) * 2U + 1U] = VTD_CONTEXT_AW_48BIT |
		((uint64_t)requester_domains[requester] << 8);
	pml4[(iova >> 39) & 0x1ffU] = (uintptr_t)pdpt | VTD_PAGE_READ_WRITE;
	pdpt[(iova >> 30) & 0x1ffU] = (uintptr_t)pd | VTD_PAGE_READ_WRITE;
	pd[(iova >> 21) & 0x1ffU] = (uintptr_t)pt | VTD_PAGE_READ_WRITE;
	for (size_t page = 0; page < requester_arena_pages[requester]; page++)
		pt[((iova >> 12) + page) & 0x1ffU] =
			(arena + page * Q35_DMA_PAGE_SIZE) | VTD_PAGE_READ_WRITE;
}

static bool table_pages_unchanged(void)
{
	if (!table_allocation)
		return false;
	for (size_t page = 0; page < Q35_DMA_TABLE_PAGES; page++) {
		const uint64_t *entries = table_page(page);

		for (size_t slot = 0; slot < Q35_DMA_PAGE_SIZE / sizeof(*entries); slot++) {
			uint64_t expected = 0;

			if (page == 0 && slot == 0U)
				expected = (uintptr_t)table_page(1) | VTD_CONTEXT_PRESENT;
			for (size_t requester = 0; requester < Q35_DMA_REQUESTERS;
			     requester++) {
				const uint16_t bdf = requester_bdfs[requester];
				const uint64_t iova = requester_iovas[requester];
				const uint64_t arena =
					(uintptr_t)requester_arenas[requester];

				if (page == 1 && slot == (bdf & 0xffU) * 2U)
					expected = (uintptr_t)table_page(
						hierarchy_page(requester, 0)) |
						VTD_CONTEXT_PRESENT;
				else if (page == 1 && slot == (bdf & 0xffU) * 2U + 1U)
					expected = VTD_CONTEXT_AW_48BIT |
						((uint64_t)requester_domains[requester] << 8);
				else if (page == hierarchy_page(requester, 0) &&
					 slot == ((iova >> 39) & 0x1ffU))
					expected = (uintptr_t)table_page(
						hierarchy_page(requester, 1)) |
						VTD_PAGE_READ_WRITE;
				else if (page == hierarchy_page(requester, 1) &&
					 slot == ((iova >> 30) & 0x1ffU))
					expected = (uintptr_t)table_page(
						hierarchy_page(requester, 2)) |
						VTD_PAGE_READ_WRITE;
				else if (page == hierarchy_page(requester, 2) &&
					 slot == ((iova >> 21) & 0x1ffU))
					expected = (uintptr_t)table_page(
						hierarchy_page(requester, 3)) |
						VTD_PAGE_READ_WRITE;
				else if (page == hierarchy_page(requester, 3) &&
					 slot >= ((iova >> 12) & 0x1ffU) &&
					 slot - ((iova >> 12) & 0x1ffU) <
					 requester_arena_pages[requester])
					expected = (arena +
						(slot - ((iova >> 12) & 0x1ffU)) *
						Q35_DMA_PAGE_SIZE) | VTD_PAGE_READ_WRITE;
			}
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

#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
static bool mor_early_root_unchanged(void)
{
	for (size_t index = 0; index < ARRAY_SIZE(mor_early_root); index++)
		if (mor_early_root[index])
			return false;
	return true;
}

static void commit_mor_early_root(void *unused)
{
	(void)unused;
	if (!(vtd_read64(Q35_VTD_ECAP) & VTD_ECAP_PAGE_WALK_COHERENT)) {
		if (clflush_supported())
			clflush_region((uintptr_t)mor_early_root,
				Q35_DMA_PAGE_SIZE);
		else
			wbinvd();
	}
	asm volatile("mfence" ::: "memory");
	mor_early_tables_committed = true;
}
#endif

static bool edu_bus_master_clear(void)
{
	return edu_requester &&
		!(pci_read_config16(edu_requester, PCI_COMMAND) & PCI_COMMAND_MASTER);
}

static bool pci_inventory(bool capture)
{
	size_t count = 0;

	for (uint32_t bus = 0; bus < CONFIG_ECAM_MMCONF_BUS_NUMBER; bus++) {
		for (uint32_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uintptr_t config = CONFIG_ECAM_MMCONF_BASE_ADDRESS +
				(bus << 20) + (devfn << 12);
			const uint32_t requester = (bus << 8) | devfn;
			uint16_t command;

			if (read16((void *)config) == 0xffff)
				continue;
			if (count == ARRAY_SIZE(pci_requesters))
				return false;
			command = read16((void *)(config + PCI_COMMAND));
			if (capture)
				write16((void *)(config + PCI_COMMAND),
					command & ~PCI_COMMAND_MASTER);
			if (read16((void *)(config + PCI_COMMAND)) & PCI_COMMAND_MASTER)
				return false;
			if (capture)
				pci_requesters[count] = requester;
			else if (count >= pci_requester_count ||
				 pci_requesters[count] != requester)
				return false;
			count++;
		}
	}
	if (capture)
		pci_requester_count = count;
	return count && count == pci_requester_count;
}

#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
static uint16_t mor_pci_read16(void *unused, uintptr_t address)
{
	(void)unused;
	return read16((void *)address);
}

static void mor_pci_write16(void *unused, uintptr_t address, uint16_t value)
{
	(void)unused;
	write16((void *)address, value);
}

static const struct q35_mor_pci_io mor_pci_io = {
	.read16 = mor_pci_read16,
	.write16 = mor_pci_write16,
};

static bool q35_mor_dma_early_default_deny(void)
{
	const struct q35_vtd_io io = {
		.context = (void *)(uintptr_t)Q35_VTD_BASE,
		.read32 = vtd_read32,
		.write32 = vtd_write32,
		.commit_tables = commit_mor_early_root,
	};
	const uintptr_t root = (uintptr_t)mor_early_root;

	if (__atomic_load_n(&mor_dma_phase, __ATOMIC_ACQUIRE) !=
		Q35_MOR_DMA_EMPTY || root > UINT32_MAX ||
	    root & (Q35_DMA_PAGE_SIZE - 1U))
		return false;
	memset(mor_early_root, 0, sizeof(mor_early_root));
	mor_early_tables_committed = false;
	if (!mor_early_root_unchanged() ||
	    q35_vtd_default_deny(&io, (uint32_t)root) ||
	    !mor_early_tables_committed || !mor_early_root_unchanged())
		return false;
	return true;
}

static bool q35_mor_dma_capture_pre_device_inventory(void)
{
	return q35_mor_pci_guard_capture(&mor_pci_io,
		CONFIG_ECAM_MMCONF_BASE_ADDRESS, CONFIG_ECAM_MMCONF_BUS_NUMBER,
		mor_pci_requesters, ARRAY_SIZE(mor_pci_requesters),
		&mor_pci_requester_count) == CB_SUCCESS;
}

static void q35_mor_dma_pre_device_guard_capture(void *unused)
{
	(void)unused;
	if (!q35_mor_dma_early_default_deny() ||
	    !q35_mor_dma_capture_pre_device_inventory()) {
		__atomic_store_n(&mor_dma_phase, Q35_MOR_DMA_FAILED,
			__ATOMIC_RELEASE);
		printk(BIOS_ERR,
		       "Q35 MOR DMA: early default-deny establishment failed\n");
		return;
	}
	__atomic_store_n(&mor_dma_phase, Q35_MOR_DMA_EARLY_DENY,
		__ATOMIC_RELEASE);
	printk(BIOS_INFO,
	       "Q35 MOR DMA: early empty-root deny active before device init\n");
}

static bool q35_mor_dma_early_root_valid(void)
{
	const uint32_t status = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
		Q35_VTD_GSTS);

	return __atomic_load_n(&mor_dma_phase, __ATOMIC_ACQUIRE) ==
		Q35_MOR_DMA_EARLY_DENY &&
		(status & (Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE)) ==
		(Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE) &&
		vtd_read64(Q35_VTD_RTADDR) == (uintptr_t)mor_early_root &&
		mor_early_root_unchanged();
}

bool q35_mor_dma_pre_device_guard_valid(void)
{
	const uint8_t phase = __atomic_load_n(&mor_dma_phase, __ATOMIC_ACQUIRE);
	const uint32_t status = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
		Q35_VTD_GSTS);
	const uintptr_t expected_root = phase == Q35_MOR_DMA_EARLY_DENY ?
		(uintptr_t)mor_early_root : (uintptr_t)table_page(0);

	return (phase == Q35_MOR_DMA_EARLY_DENY ||
		phase == Q35_MOR_DMA_FINAL_ACTIVE) &&
		(status & (Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE)) ==
		(Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE) &&
		vtd_read64(Q35_VTD_RTADDR) == expected_root &&
		mor_early_root_unchanged() &&
		q35_mor_pci_guard_validate(&mor_pci_io,
			CONFIG_ECAM_MMCONF_BASE_ADDRESS,
			CONFIG_ECAM_MMCONF_BUS_NUMBER, mor_pci_requesters,
			mor_pci_requester_count) == CB_SUCCESS;
}

static bool q35_mor_dma_requiesce_after_device_init(void)
{
	if (!q35_mor_dma_early_root_valid() ||
	    q35_mor_pci_guard_requiesce(&mor_pci_io,
		CONFIG_ECAM_MMCONF_BASE_ADDRESS,
		CONFIG_ECAM_MMCONF_BUS_NUMBER, mor_pci_requesters,
		mor_pci_requester_count) != CB_SUCCESS ||
	    !q35_mor_dma_early_root_valid() ||
	    q35_mor_pci_guard_validate(&mor_pci_io,
		CONFIG_ECAM_MMCONF_BASE_ADDRESS,
		CONFIG_ECAM_MMCONF_BUS_NUMBER, mor_pci_requesters,
		mor_pci_requester_count) != CB_SUCCESS) {
		__atomic_store_n(&mor_dma_phase, Q35_MOR_DMA_FAILED,
			__ATOMIC_RELEASE);
		return false;
	}
	printk(BIOS_INFO,
	       "Q35 MOR DMA: retained PCI topology revalidated and BME re-quiesced under early deny\n");
	return true;
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_ENTRY,
	q35_mor_dma_pre_device_guard_capture, NULL);

static int q35_mor_dma_switch_protected_root(const struct q35_vtd_io *io,
	uint32_t final_root)
{
	const uint32_t early_root = (uintptr_t)mor_early_root;
	int status;

	if (__atomic_load_n(&mor_dma_phase, __ATOMIC_ACQUIRE) !=
		Q35_MOR_DMA_EARLY_DENY || !q35_mor_dma_pre_device_guard_valid() ||
	    !mor_early_root_unchanged()) {
		__atomic_store_n(&mor_dma_phase, Q35_MOR_DMA_FAILED,
			__ATOMIC_RELEASE);
		return -1;
	}
	status = q35_vtd_switch_root(io, early_root, final_root);
	if (status)
		__atomic_store_n(&mor_dma_phase, Q35_MOR_DMA_FAILED,
			__ATOMIC_RELEASE);
	return status;
}
#endif

static bool vtd_runtime_state_valid(void)
{
	const uint32_t status = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
		Q35_VTD_GSTS);
	const uint64_t root = vtd_read64(Q35_VTD_RTADDR);
	const struct q35_dma_pmr_state pmr = {
		.enable = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
			Q35_VTD_PMEN),
		.low_base = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
			Q35_VTD_PLMBASE),
		.low_limit = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
			Q35_VTD_PLMLIMIT),
		.high_base = vtd_read64(Q35_VTD_PHMBASE),
		.high_limit = vtd_read64(Q35_VTD_PHMLIMIT),
	};

	return (status & (Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE)) ==
		(Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE) &&
		root == (uintptr_t)table_page(0) &&
		!(pmr.enable & (Q35_VTD_PMR_ENABLE | Q35_VTD_PMR_STATUS)) &&
		q35_dma_pmr_state_matches(&pmr_snapshot, &pmr) &&
		table_pages_unchanged() && pci_inventory(false);
}

#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
enum cb_err q35_mor_dma_snapshot(
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	uint64_t identity = 0xcbf29ce484222325ULL;
	const uint64_t generation = payload_resource_revision4_generation();
	const uint32_t status = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
		Q35_VTD_GSTS);
	const uint64_t root = vtd_read64(Q35_VTD_RTADDR);
	const struct q35_dma_pmr_state pmr = {
		.enable = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_PMEN),
		.low_base = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
			Q35_VTD_PLMBASE),
		.low_limit = vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
			Q35_VTD_PLMLIMIT),
		.high_base = vtd_read64(Q35_VTD_PHMBASE),
		.high_limit = vtd_read64(Q35_VTD_PHMLIMIT),
	};
	const uint8_t *table = table_allocation;

	if (!snapshot || !backend_ready || !generation ||
	    __atomic_load_n(&mor_dma_phase, __ATOMIC_ACQUIRE) !=
		Q35_MOR_DMA_FINAL_ACTIVE ||
	    !payload_resource_revision4_published() || !vtd_runtime_state_valid() ||
	    !q35_mor_dma_pre_device_guard_valid())
		return CB_ERR;
	identity ^= mor_pci_requester_count;
	identity *= 0x100000001b3ULL;
	for (size_t index = 0; index < mor_pci_requester_count; index++) {
		identity ^= mor_pci_requesters[index].bdf;
		identity *= 0x100000001b3ULL;
		identity ^= mor_pci_requesters[index].vendor_id;
		identity *= 0x100000001b3ULL;
		identity ^= mor_pci_requesters[index].device_id;
		identity *= 0x100000001b3ULL;
	}
	identity ^= (uintptr_t)mor_early_root;
	identity *= 0x100000001b3ULL;
	identity ^= __atomic_load_n(&mor_dma_phase, __ATOMIC_ACQUIRE);
	identity *= 0x100000001b3ULL;
	for (size_t index = 0; index < sizeof(mor_early_root); index++) {
		identity ^= ((const uint8_t *)mor_early_root)[index];
		identity *= 0x100000001b3ULL;
	}
	identity ^= status;
	identity *= 0x100000001b3ULL;
	identity ^= root;
	identity *= 0x100000001b3ULL;
	identity ^= pmr.enable;
	identity *= 0x100000001b3ULL;
	identity ^= pmr.low_base;
	identity *= 0x100000001b3ULL;
	identity ^= pmr.low_limit;
	identity *= 0x100000001b3ULL;
	identity ^= pmr.high_base;
	identity *= 0x100000001b3ULL;
	identity ^= pmr.high_limit;
	identity *= 0x100000001b3ULL;
	for (size_t index = 0; index < Q35_DMA_TABLE_BYTES; index++) {
		identity ^= table[index];
		identity *= 0x100000001b3ULL;
	}
	if (!mor_early_root_unchanged() || !table_pages_unchanged())
		return CB_ERR;
	identity ^= generation;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->generation = generation;
	for (size_t index = 0; index < sizeof(snapshot->identity); index++) {
		identity ^= identity >> 12;
		identity ^= identity << 25;
		identity ^= identity >> 27;
		snapshot->identity[index] = identity;
	}
	return CB_SUCCESS;
}
#endif

static bool dma_page_denied(size_t requester, uint64_t address)
{
	const uint64_t *pml4 = table_page(hierarchy_page(requester, 0));
	const uint64_t *pdpt = table_page(hierarchy_page(requester, 1));
	const uint64_t *pd = table_page(hierarchy_page(requester, 2));
	const uint64_t *pt = table_page(hierarchy_page(requester, 3));
	uint64_t entry;

	entry = pml4[(address >> 39) & 0x1ffU];
	if (!(entry & VTD_CONTEXT_PRESENT))
		return true;
	if ((entry & ~0xfffULL) != (uintptr_t)pdpt)
		return false;
	entry = pdpt[(address >> 30) & 0x1ffU];
	if (!(entry & VTD_CONTEXT_PRESENT))
		return true;
	if ((entry & ~0xfffULL) != (uintptr_t)pd)
		return false;
	entry = pd[(address >> 21) & 0x1ffU];
	if (!(entry & VTD_CONTEXT_PRESENT))
		return true;
	if ((entry & ~0xfffULL) != (uintptr_t)pt)
		return false;
	return !(pt[(address >> 12) & 0x1ffU] & VTD_CONTEXT_PRESENT);
}

static bool dma_range_denied(size_t requester, uint64_t base, uint64_t size)
{
	uint64_t end;
	uint64_t page;

	if (!size || base > UINT64_MAX - size)
		return false;
	end = base + size;
	page = base & ~(uint64_t)(Q35_DMA_PAGE_SIZE - 1U);
	while (page < end) {
		if (!dma_page_denied(requester, page))
			return false;
		if (page > UINT64_MAX - Q35_DMA_PAGE_SIZE)
			return false;
		page += Q35_DMA_PAGE_SIZE;
	}
	return true;
}

static bool all_requesters_deny_range(uint64_t base, uint64_t size)
{
	for (size_t requester = 0; requester < Q35_DMA_REQUESTERS; requester++)
		if (!dma_range_denied(requester, base, size))
			return false;
	return true;
}

static void clear_dma_fault(uint32_t fault_offset)
{
	write32((void *)(uintptr_t)(Q35_VTD_BASE + fault_offset + 12U), 1U << 31);
	write32((void *)(uintptr_t)(Q35_VTD_BASE + Q35_VTD_FSTS),
		Q35_VTD_FAULT_PENDING);
}

static bool edu_dma_write(uint64_t destination)
{
	struct resource *bar = probe_resource(edu_requester, PCI_BASE_ADDRESS_0);
	uint8_t *mmio;
	uint16_t command;

	if (!bar || (bar->flags & (IORESOURCE_MEM | IORESOURCE_ASSIGNED)) !=
	    (IORESOURCE_MEM | IORESOURCE_ASSIGNED) || bar->size < 0x100000 ||
	    destination > UINT32_MAX)
		return false;
	mmio = res2mmio(bar, 0, 0);
	command = pci_read_config16(edu_requester, PCI_COMMAND);
	pci_write_config16(edu_requester, PCI_COMMAND,
		command | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	if (!(pci_read_config16(edu_requester, PCI_COMMAND) & PCI_COMMAND_MASTER))
		return false;
	write64(mmio + EDU_DMA_DESTINATION, destination);
	write64(mmio + EDU_DMA_COUNT, EDU_TEST_BYTES);
	write64(mmio + EDU_DMA_COMMAND, EDU_DMA_FROM_DEVICE);
	mdelay(200);
	write64(mmio + EDU_DMA_COMMAND, 0);
	pci_write_config16(edu_requester, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
	return edu_bus_master_clear();
}

static bool prove_range_fault(uint64_t base, uint64_t size)
{
	uint8_t before[EDU_TEST_BYTES];
	uint64_t cap;
	uint64_t fault_low;
	uint64_t fault_high;
	uint32_t fault_offset;

	if (size < sizeof(before) || base > UINTPTR_MAX ||
	    !all_requesters_deny_range(base, size))
		return false;
	memcpy(before, (const void *)(uintptr_t)base, sizeof(before));
	cap = vtd_read64(Q35_VTD_CAP);
	fault_offset = (uint32_t)((cap >> 24) & 0x3ffU) * 16U;
	if (fault_offset < 0x40U || fault_offset > 0xff0U ||
	    (vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_FSTS) &
	     Q35_VTD_FAULT_PENDING) ||
	    !edu_dma_write(base))
		return false;
	fault_low = vtd_read64(fault_offset);
	fault_high = vtd_read64(fault_offset + 8U);
	if (!(vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_FSTS) &
	      Q35_VTD_FAULT_PENDING) || !(fault_high & (1ULL << 63)) ||
	    (fault_high & 0xffffU) != Q35_EDU_BDF ||
	    ((fault_high >> 32) & 0xffU) != 2U ||
	    (fault_low & ~0xfffULL) != (base & ~0xfffULL) ||
	    memcmp(before, (const void *)(uintptr_t)base, sizeof(before)))
		return false;
	clear_dma_fault(fault_offset);
	return !(vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_FSTS) &
		Q35_VTD_FAULT_PENDING);
}

static bool capsule_dma_proof(void)
{
	return vtd_runtime_state_valid() &&
		all_requesters_deny_range(capsule_geometry.communication_base,
			capsule_geometry.communication_reserved_size) &&
		all_requesters_deny_range(capsule_geometry.staging_base,
			capsule_geometry.staging_size) &&
		prove_range_fault(capsule_geometry.communication_base,
			capsule_geometry.communication_reserved_size) &&
		prove_range_fault(capsule_geometry.staging_base,
			capsule_geometry.staging_size) && vtd_runtime_state_valid();
}

bool q35_capsule_dma_protected(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	return CONFIG(Q35_CAPSULE_DMA_TEST_PROOF) && backend_ready &&
		q35_capsule_dma_range_valid(&capsule_geometry, base, size) &&
		capsule_dma_proof();
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
	write64(mmio + EDU_DMA_COMMAND, 0);
	pci_write_config16(edu_requester, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
	if (!edu_bus_master_clear())
		die("Q35 DMA: EDU denial probe left BME active");

	cap = vtd_read64(Q35_VTD_CAP);
	fault_offset = (uint32_t)((cap >> 24) & 0x3ffU) * 16U;
	if (fault_offset < 0x40U || fault_offset > 0xff0U)
		die("Q35 DMA: invalid fault-record offset");
	fault_high = vtd_read64(fault_offset + 8U);
	if (!(vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_FSTS) &
	      Q35_VTD_FAULT_PENDING) || !(fault_high & (1ULL << 63)) ||
	    (fault_high & 0xffffU) != Q35_EDU_BDF ||
	    ((fault_high >> 32) & 0xffU) != 2U)
		die("Q35 DMA: unlisted EDU DMA did not fault at its absent context");
	for (size_t byte = 0; byte < EDU_TEST_BYTES; byte++)
		if (dma_target[byte] != 0x5a)
			die("Q35 DMA: denied EDU DMA changed the target");
	clear_dma_fault(fault_offset);
	if (vtd_read32((void *)(uintptr_t)Q35_VTD_BASE, Q35_VTD_FSTS) &
	    Q35_VTD_FAULT_PENDING)
		die("Q35 DMA: EDU denial fault did not clear");
	printk(BIOS_INFO,
	       "Q35 DMA: denied unlisted EDU %#x, reason %#llx, target unchanged, BME clear\n",
	       Q35_EDU_BDF, (unsigned long long)((fault_high >> 32) & 0xffU));
}

static struct device *exact_device(uint16_t vendor, uint16_t device,
	uint16_t expected_bdf)
{
	struct device *found = dev_find_device(vendor, device, NULL);
	struct device *duplicate = found ? dev_find_device(vendor, device, found) : NULL;
	uint16_t bdf;

	if (!found || duplicate || !found->upstream ||
	    found->upstream->segment_group != 0 ||
	    found->upstream->secondary > UINT8_MAX ||
	    found->path.pci.devfn > UINT8_MAX)
		return NULL;
	bdf = ((uint16_t)found->upstream->secondary << 8) |
		(uint16_t)found->path.pci.devfn;
	return bdf == expected_bdf ? found : NULL;
}

static void q35_dma_backend_enable(void *unused)
{
	const struct q35_vtd_io io = {
		.context = (void *)(uintptr_t)Q35_VTD_BASE,
		.read32 = vtd_read32,
		.write32 = vtd_write32,
		.commit_tables = commit_vtd_tables,
	};
	const struct cbmem_entry *blob_entry;
	const struct cbmem_entry *table_entry;
	const struct cbmem_entry *arena_entry;
	uint8_t *root;
	uintptr_t arena_base;
	int result;

	(void)unused;
#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
	if (!q35_mor_dma_requiesce_after_device_init())
		die("Q35 MOR DMA: device-init PCI re-quiesce failed");
#endif
	dma_devices[0] = exact_device(QEMU_VENDOR, QEMU_NVME_DEVICE, Q35_NVME_BDF);
	dma_devices[1] = exact_device(QEMU_VENDOR, QEMU_XHCI_DEVICE, Q35_XHCI_BDF);
	edu_requester = exact_device(EDU_VENDOR, EDU_DEVICE, Q35_EDU_BDF);
	if (!dma_devices[0])
		die("Q35 DMA: expected exactly one NVMe requester at 0000:00:03.0");
	if (!dma_devices[1])
		die("Q35 DMA: expected exactly one XHCI requester at 0000:00:04.0");
	if (!edu_requester)
		die("Q35 DMA: expected exactly one unlisted EDU at 0000:00:05.0");
	requester_bdfs[0] = Q35_NVME_BDF;
	requester_bdfs[1] = Q35_XHCI_BDF;
	for (size_t requester = 0; requester < Q35_DMA_REQUESTERS; requester++)
		pci_write_config16(dma_devices[requester], PCI_COMMAND,
			pci_read_config16(dma_devices[requester], PCI_COMMAND) &
			~PCI_COMMAND_MASTER);
	pci_write_config16(edu_requester, PCI_COMMAND,
		pci_read_config16(edu_requester, PCI_COMMAND) & ~PCI_COMMAND_MASTER);
	if (!edu_bus_master_clear())
		die("Q35 DMA: EDU BME could not be cleared");

	if (CONFIG(PAYLOAD_DMA_HANDOFF)) {
		blob_entry = cbmem_entry_add(CBMEM_ID_DMA_HANDOFF, Q35_DMA_BLOB_BYTES);
		if (!blob_entry || cbmem_entry_size(blob_entry) != Q35_DMA_BLOB_BYTES)
			die("Q35 DMA: modern handoff allocation size is not exact");
		handoff_allocation = cbmem_entry_start(blob_entry);
	}
	table_entry = cbmem_entry_add(CBMEM_ID_Q35_VTD_TABLES, Q35_DMA_TABLE_BYTES);
	if (!table_entry || cbmem_entry_size(table_entry) != Q35_DMA_TABLE_BYTES)
		die("Q35 DMA: resident table allocation size is not exact");
	table_allocation = cbmem_entry_start(table_entry);
	arena_entry = cbmem_entry_add(CBMEM_ID_Q35_DMA_ARENAS,
		Q35_DMA_ARENA_BYTES);
	if (!arena_entry || cbmem_entry_size(arena_entry) != Q35_DMA_ARENA_BYTES)
		die("Q35 DMA: resident arena allocation size is not exact");
	arena_allocation = cbmem_entry_start(arena_entry);
	if (!handoff_allocation || !table_allocation || !arena_allocation ||
	    ((uintptr_t)table_allocation & (Q35_DMA_PAGE_SIZE - 1U)))
		die("Q35 DMA: aligned split allocations failed");
	if (handoff_allocation)
		memset(handoff_allocation, 0, Q35_DMA_BLOB_BYTES);
	memset(table_allocation, 0, Q35_DMA_TABLE_BYTES);
	memset(arena_allocation, 0, Q35_DMA_ARENA_BYTES);
	arena_base = ALIGN_UP((uintptr_t)arena_allocation, Q35_DMA_PAGE_SIZE);
	requester_arenas[0] = (void *)arena_base;
	requester_arenas[1] = requester_arenas[0] +
		Q35_NVME_ARENA_PAGES * Q35_DMA_PAGE_SIZE;
	if ((uintptr_t)requester_arenas[1] +
	    Q35_XHCI_ARENA_PAGES * Q35_DMA_PAGE_SIZE >
	    (uintptr_t)arena_allocation + Q35_DMA_ARENA_BYTES)
		die("Q35 DMA: aligned arena suballocations exceed reservation");
	root = table_allocation;
	if ((uintptr_t)root > UINT32_MAX - Q35_DMA_TABLE_BYTES)
		die("Q35 DMA: QEMU legacy tables are not wholly below 4 GiB");
	if (!(vtd_read64(Q35_VTD_CAP) & VTD_CAP_SAGAW_48BIT))
		die("Q35 DMA: VT-d lacks the required 48-bit address width");
	for (size_t requester = 0; requester < Q35_DMA_REQUESTERS; requester++)
		populate_requester_hierarchy(requester);
	if (!table_pages_unchanged())
		die("Q35 DMA: requester deny hierarchy is malformed");

#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
	result = q35_mor_dma_switch_protected_root(&io,
		(uint32_t)(uintptr_t)root);
	if (result)
		die("Q35 MOR DMA: protected-root switch failed %d", result);
#else
	result = q35_vtd_default_deny(&io, (uint32_t)(uintptr_t)root);
	if (result)
		die("Q35 DMA: default-deny enable failed %d", result);
#endif
	if (!tables_committed)
		die("Q35 DMA: VT-d tables were not committed before enable");
#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
	__atomic_store_n(&mor_dma_phase, Q35_MOR_DMA_FINAL_ACTIVE,
		__ATOMIC_RELEASE);
	printk(BIOS_INFO,
	       "Q35 MOR DMA: empty-root deny switched to final protected root without disabling translation\n");
#endif
	printk(BIOS_INFO, "Q35 DMA: %s page-walk table visibility established\n",
	       noncoherent_writeback ? "noncoherent" : "coherent");
	prove_requester_denied();
	if (!(vtd_read32(io.context, Q35_VTD_GSTS) &
	      Q35_VTD_TRANSLATION_ENABLE) || !table_pages_unchanged() ||
	    !edu_bus_master_clear())
		die("Q35 DMA: protected state did not survive read-back");
	pmr_snapshot = (struct q35_dma_pmr_state) {
		.enable = vtd_read32(io.context, Q35_VTD_PMEN),
		.low_base = vtd_read32(io.context, Q35_VTD_PLMBASE),
		.low_limit = vtd_read32(io.context, Q35_VTD_PLMLIMIT),
		.high_base = vtd_read64(Q35_VTD_PHMBASE),
		.high_limit = vtd_read64(Q35_VTD_PHMLIMIT),
	};
	if (pmr_snapshot.enable & (Q35_VTD_PMR_ENABLE | Q35_VTD_PMR_STATUS))
		die("Q35 DMA: PMR state is active");
#if CONFIG(Q35_PAYLOAD_MM_MOR_LINEAR_TEST_PROVIDER)
	if (!q35_mor_dma_pre_device_guard_valid())
		die("Q35 MOR: pre-device PCI guard changed");
	if (mor_pci_requester_count > ARRAY_SIZE(pci_requesters))
		die("Q35 MOR DMA: retained PCI inventory exceeds runtime storage");
	for (size_t index = 0; index < mor_pci_requester_count; index++)
		pci_requesters[index] = mor_pci_requesters[index].bdf;
	pci_requester_count = mor_pci_requester_count;
#else
	if (!pci_inventory(true))
		die("Q35 DMA: PCI requester inventory is unsafe");
#endif
	backend_ready = true;
	printk(BIOS_INFO,
	       "Q35 DMA: default-deny active, %zu PCI functions BME clear, NVMe 0000:00:03.0 32 pages at %#llx, XHCI 0000:00:04.0 128 pages at %#llx, EDU unlisted\n",
	       pci_requester_count, (unsigned long long)Q35_NVME_IOVA,
	       (unsigned long long)Q35_XHCI_IOVA);
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, q35_dma_backend_enable, NULL);

static void q35_capsule_dma_proof_enable(void *unused)
{
	struct capsule_broker_buffer_reservation reservation;
	FWCfgFile injection;

	(void)unused;
	if (!CONFIG(Q35_CAPSULE_DMA_TEST_PROOF))
		return;
	if (!capsule_broker_buffers_get(&reservation))
		die("Q35 capsule DMA: fixed buffers unavailable");
	capsule_geometry = (struct q35_capsule_dma_geometry) {
		.communication_base = reservation.communication_base,
		.communication_reserved_size =
			reservation.communication_reserved_size,
		.communication_size = reservation.communication_size,
		.staging_base = reservation.staging_base,
		.staging_size = reservation.staging_size,
	};
	if (!fw_cfg_check_file(&injection, "opt/q35/capsule-dma-fail"))
		pmr_snapshot.high_base ^= 1ULL << 32;
	if (!q35_capsule_dma_protected(NULL, capsule_geometry.communication_base,
		capsule_geometry.communication_size))
		die("Q35 capsule DMA: exact-range proof failed");
	printk(BIOS_INFO,
	       "Q35 capsule DMA: communication %#llx/%#llx and staging %#llx/%#llx denied to both admitted requesters; unlisted EDU fault passed\n",
	       (unsigned long long)capsule_geometry.communication_base,
	       (unsigned long long)capsule_geometry.communication_reserved_size,
	       (unsigned long long)capsule_geometry.staging_base,
	       (unsigned long long)capsule_geometry.staging_size);
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_EXIT,
	q35_capsule_dma_proof_enable, NULL);

bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	struct dma_handoff_requester requester[Q35_DMA_REQUESTERS];
	const uint64_t generation = payload_resource_revision4_generation();
	const struct q35_dma_facts facts = {
		.generation = generation,
		.resource_generation = payload_resource_revision4_generation(),
		.requesters = requester_bdfs,
		.requester_count = ARRAY_SIZE(requester_bdfs),
		.translation_active = !!(vtd_read32((void *)(uintptr_t)Q35_VTD_BASE,
			Q35_VTD_GSTS) & Q35_VTD_TRANSLATION_ENABLE),
		.tables_resident = handoff_allocation != NULL &&
			table_allocation != NULL && arena_allocation != NULL,
		.tables_unchanged = table_pages_unchanged(),
		.bus_master_clear = pci_inventory(false),
	};
	size_t written;

	if (!address || !bytes || !backend_ready ||
	    !payload_resource_revision4_published() || !q35_dma_facts_valid(&facts))
		return false;
	memset(requester, 0, sizeof(requester));
	for (size_t index = 0; index < ARRAY_SIZE(requester); index++) {
		requester[index].bdf = requester_bdfs[index];
		requester[index].protection_domain = requester_domains[index];
		requester[index].flags = DMA_HANDOFF_REQUESTER_FLAGS;
		requester[index].arena_cpu_base =
			(uintptr_t)requester_arenas[index];
		requester[index].arena_device_base = requester_iovas[index];
		requester[index].arena_pages = requester_arena_pages[index];
		requester[index].arena_flags = DMA_HANDOFF_ARENA_FLAGS;
	}
	if (dma_handoff_build(handoff_allocation, Q35_DMA_BLOB_BYTES, generation,
		requester, ARRAY_SIZE(requester), &written) != CB_SUCCESS)
		return false;
	*address = (uintptr_t)handoff_allocation;
	*bytes = written;
	printk(BIOS_INFO,
	       "Q35 DMA: handoff generation %llu, domains 1/2 linked, ten immutable table pages, 32/128 immutable arena pages\n",
	       (unsigned long long)generation);
	return true;
}
