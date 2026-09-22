/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/iommu.h>
#include <amdblocks/iommu_dma.h>
#include <boot/dma_handoff.h>
#include <bootstate.h>
#include <cbmem.h>
#include <commonlib/dma_handoff.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/cache.h>
#include <device/device.h>
#include <device/mmio.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <device/resource.h>
#include <halt.h>
#include <soc/dma_binding.h>
#include <soc/dma_guard.h>
#include <soc/dma_handoff.h>
#include <soc/dma_policy.h>
#include <string.h>

#define CEZANNE_ARENA_DEVICE_BASE AMD_IOMMU_PAGE_SIZE

struct cezanne_iommu_context {
	uint8_t *registers;
};

static struct amd_iommu_dma_requester dma_requesters[AMD_IOMMU_DMA_MAX_REQUESTERS];
static struct cezanne_dma_policy boot_policy;
static struct cezanne_iommu_context iommu_context;
static struct cezanne_dma_pci_snapshot pci_snapshot;
static struct dma_handoff_requester public_requesters[AMD_IOMMU_DMA_MAX_REQUESTERS];
static uint8_t *handoff_blob;
static uint8_t *device_table;
static uint8_t *page_tables;
static size_t handoff_bytes;
static size_t device_table_bytes;
static size_t page_table_bytes;
static size_t requester_count;
static bool backend_ready;

_Static_assert(AMD_IOMMU_DMA_MAX_REQUESTERS == DMA_HANDOFF_MAX_REQUESTERS,
	"AMD backend and public handoff bounds differ");
_Static_assert(AMD_IOMMU_DMA_MAX_REQUESTERS == CEZANNE_DMA_POLICY_MAX_CONTROLLERS,
	"AMD backend and Cezanne policy bounds differ");
_Static_assert(CONFIG_ECAM_MMCONF_BUS_NUMBER <= 256,
	"Cezanne DMA supports one PCI segment");

/* A board must identify the exact payload-owned controllers by topology. */
__weak enum cezanne_dma_boot_controller mainboard_cezanne_dma_boot_controller(
	const struct device *device, uint16_t *priority)
{
	(void)device;
	(void)priority;
	return CEZANNE_DMA_BOOT_NONE;
}

static bool board_boot_controller(const struct device *device, uint16_t *priority,
	enum cezanne_dma_boot_controller *controller)
{
	uint16_t selected_priority = 0;

	*controller = mainboard_cezanne_dma_boot_controller(device, &selected_priority);
	if (!selected_priority || *controller == CEZANNE_DMA_BOOT_NONE)
		return false;
	if (priority)
		*priority = selected_priority;
	return true;
}

bool payload_resource_revision4_ready(void)
{
	return backend_ready;
}

bool payload_resource_boot_controller(const struct device *device, uint16_t *priority)
{
	return cezanne_dma_policy_lookup(&boot_policy, device, priority);
}

static bool pci_identity(const struct device *device, uint16_t *segment, uint16_t *bdf)
{
	if (!device || !device->enabled || device->path.type != DEVICE_PATH_PCI ||
	    !device->upstream || device->upstream->segment_group > UINT16_MAX ||
	    device->upstream->secondary > UINT8_MAX || device->path.pci.devfn > UINT8_MAX)
		return false;
	*segment = device->upstream->segment_group;
	*bdf = ((uint16_t)device->upstream->secondary << 8) |
		device->path.pci.devfn;
	return true;
}

static uint16_t ecam_read_vendor(void *unused, uint16_t bdf)
{
	(void)unused;
	return pci_s_read_config16(PCI_DEV(bdf >> 8, PCI_SLOT(bdf), PCI_FUNC(bdf)),
		PCI_VENDOR_ID);
}

static uint16_t ecam_read_command(void *unused, uint16_t bdf)
{
	(void)unused;
	return pci_s_read_config16(PCI_DEV(bdf >> 8, PCI_SLOT(bdf), PCI_FUNC(bdf)),
		PCI_COMMAND);
}

static uint16_t ecam_read_device(void *unused, uint16_t bdf)
{
	(void)unused;
	return pci_s_read_config16(PCI_DEV(bdf >> 8, PCI_SLOT(bdf), PCI_FUNC(bdf)),
		PCI_DEVICE_ID);
}

static uint32_t ecam_read_class_revision(void *unused, uint16_t bdf)
{
	(void)unused;
	return pci_s_read_config32(PCI_DEV(bdf >> 8, PCI_SLOT(bdf), PCI_FUNC(bdf)),
		PCI_CLASS_REVISION);
}

static void ecam_write_command(void *unused, uint16_t bdf, uint16_t command)
{
	(void)unused;
	pci_s_write_config16(PCI_DEV(bdf >> 8, PCI_SLOT(bdf), PCI_FUNC(bdf)),
		PCI_COMMAND, command);
}

static const struct cezanne_dma_pci_io pci_io = {
	.read_vendor = ecam_read_vendor,
	.read_device = ecam_read_device,
	.read_class_revision = ecam_read_class_revision,
	.read_command = ecam_read_command,
	.write_command = ecam_write_command,
};

static bool pci_bus_masters_clear(void *unused)
{
	(void)unused;
	return cezanne_dma_pci_quiescence_held(&pci_io, &pci_snapshot);
}

static uint64_t iommu_read64(void *context, uint32_t offset)
{
	const struct cezanne_iommu_context *iommu = context;

	return read64(iommu->registers + offset);
}

static void iommu_write64(void *context, uint32_t offset, uint64_t value)
{
	const struct cezanne_iommu_context *iommu = context;

	write64(iommu->registers + offset, value);
}

static void commit_iommu_tables(void *context, const void *base, size_t bytes)
{
	(void)context;
	(void)base;
	(void)bytes;
	wbinvd();
	asm volatile("mfence" ::: "memory");
}

static void __noreturn fail_closed(void *unused)
{
	(void)unused;
	die("Cezanne DMA: terminal IOMMU ownership-transfer failure");
}

static const struct amd_iommu_dma_io iommu_io = {
	.context = &iommu_context,
	.read64 = iommu_read64,
	.write64 = iommu_write64,
	.commit_tables = commit_iommu_tables,
	.quiescence_held = pci_bus_masters_clear,
	.fail_closed = fail_closed,
};

static void allocate_dma_state(uint16_t maximum_device_id, size_t arena_pages)
{
	const struct cbmem_entry *entry;
	size_t arena_bytes;
	size_t table_bytes;
	uintptr_t aligned;
	uint8_t *allocation;

	device_table_bytes = amd_iommu_device_table_bytes(maximum_device_id);
	if (!requester_count || requester_count > SIZE_MAX / AMD_IOMMU_PAGE_SIZE ||
	    arena_pages > SIZE_MAX / AMD_IOMMU_PAGE_SIZE)
		die("Cezanne DMA: allocation size overflow");
	page_table_bytes = requester_count * AMD_IOMMU_PAGE_SIZE;
	arena_bytes = arena_pages * AMD_IOMMU_PAGE_SIZE;
	if (device_table_bytes > SIZE_MAX - page_table_bytes -
	    (AMD_IOMMU_PAGE_SIZE - 1U))
		die("Cezanne DMA: table allocation size overflow");
	table_bytes = device_table_bytes + page_table_bytes +
		(AMD_IOMMU_PAGE_SIZE - 1U);
	handoff_bytes = sizeof(struct dma_handoff_header) +
		requester_count * sizeof(struct dma_handoff_requester);

	entry = cbmem_entry_add(CBMEM_ID_AMD_IOMMU_TABLES, table_bytes);
	if (!entry || !cezanne_dma_cbmem_layout((uintptr_t)cbmem_entry_start(entry),
		cbmem_entry_size(entry), device_table_bytes + page_table_bytes,
		AMD_IOMMU_PAGE_SIZE, &aligned))
		die("Cezanne DMA: table allocation failed");
	allocation = (void *)aligned;
	device_table = allocation;
	page_tables = device_table + device_table_bytes;

	if (arena_bytes > SIZE_MAX - (AMD_IOMMU_PAGE_SIZE - 1U))
		die("Cezanne DMA: arena allocation size overflow");
	entry = cbmem_entry_add(CBMEM_ID_AMD_IOMMU_ARENAS,
		arena_bytes + AMD_IOMMU_PAGE_SIZE - 1U);
	if (!entry || !cezanne_dma_cbmem_layout((uintptr_t)cbmem_entry_start(entry),
		cbmem_entry_size(entry), arena_bytes, AMD_IOMMU_PAGE_SIZE,
		&aligned))
		die("Cezanne DMA: arena allocation failed");
	allocation = (void *)aligned;
	{
		uint8_t *arena = allocation;
		size_t offset = 0;

		for (size_t index = 0; index < requester_count; index++) {
			dma_requesters[index].arena_cpu_base = (uintptr_t)arena + offset;
			dma_requesters[index].arena_device_base = CEZANNE_ARENA_DEVICE_BASE;
			offset += (size_t)dma_requesters[index].arena_pages *
				AMD_IOMMU_PAGE_SIZE;
		}
		memset(arena, 0, arena_bytes);
	}

	entry = cbmem_entry_add(CBMEM_ID_DMA_HANDOFF, handoff_bytes);
	if (!entry || cbmem_entry_size(entry) != handoff_bytes)
		die("Cezanne DMA: handoff allocation failed");
	handoff_blob = cbmem_entry_start(entry);
	memset(handoff_blob, 0, handoff_bytes);
}

static void collect_boot_requesters(size_t *arena_pages, uint16_t *maximum_device_id)
{
	const struct device *device;

	*arena_pages = 0;
	*maximum_device_id = 0;
	if (boot_policy.frozen)
		die("Cezanne DMA: boot policy was already frozen");
	requester_count = 0;
	for (device = all_devices; device; device = device->next) {
		enum cezanne_dma_boot_controller controller;
		uint32_t pages;
		uint16_t bdf;
		uint16_t priority;
		uint16_t segment;

		if (!device->enabled ||
		    !board_boot_controller(device, &priority, &controller))
			continue;
		if (requester_count >= ARRAY_SIZE(dma_requesters) ||
		    !pci_identity(device, &segment, &bdf) || segment != 0 ||
		    !cezanne_dma_requester_in_aperture(bdf,
			CONFIG_ECAM_MMCONF_BUS_NUMBER * 256U) ||
		    cezanne_dma_policy_add(&boot_policy, device, device->class,
			controller, priority, &pages) != CB_SUCCESS ||
		    *arena_pages > SIZE_MAX - pages)
			die("Cezanne DMA: invalid boot-requester inventory");
		dma_requesters[requester_count] = (struct amd_iommu_dma_requester) {
			.device_id = bdf,
			.protection_domain = requester_count + 1U,
			.arena_pages = pages,
		};
		requester_count++;
		*arena_pages += pages;
		*maximum_device_id = MAX(*maximum_device_id, bdf);
	}
	if (!requester_count || !*arena_pages)
		die("Cezanne DMA: no runtime boot requester");
	if (!cezanne_dma_policy_freeze(&boot_policy))
		die("Cezanne DMA: boot policy cannot be frozen");
}

static void cezanne_dma_enable(void *unused)
{
	struct device *iommu_device;
	struct resource *iommu_resource;
	uint32_t base_high;
	uint32_t base_low;
	size_t arena_pages;
	uint16_t maximum_device_id;

	(void)unused;
	collect_boot_requesters(&arena_pages, &maximum_device_id);
	allocate_dma_state(maximum_device_id, arena_pages);
	if (amd_iommu_build_dma_state(device_table, device_table_bytes,
		page_tables, page_table_bytes, dma_requesters, requester_count) != CB_SUCCESS)
		die("Cezanne DMA: owned translation tables are invalid");

	iommu_device = pcidev_on_root(0, 2);
	iommu_resource = iommu_device ?
		probe_resource(iommu_device, IOMMU_CAP_BASE_LO) : NULL;
	if (!iommu_device || !iommu_device->enabled || !iommu_resource ||
	    (iommu_resource->flags & (IORESOURCE_MEM | IORESOURCE_ASSIGNED)) !=
		(IORESOURCE_MEM | IORESOURCE_ASSIGNED))
		die("Cezanne DMA: live IOMMU resource is unavailable");
	base_low = pci_read_config32(iommu_device, IOMMU_CAP_BASE_LO);
	base_high = pci_read_config32(iommu_device, IOMMU_CAP_BASE_HI);
	if (!cezanne_dma_iommu_resource_valid(iommu_resource->base,
		iommu_resource->size, base_low, base_high, UINTPTR_MAX))
		die("Cezanne DMA: programmed IOMMU resource does not match coreboot");
	iommu_context.registers = res2mmio(iommu_resource, 0, 0);

	if (!cezanne_dma_pci_quiesce(&pci_io, &pci_snapshot,
		CONFIG_ECAM_MMCONF_BUS_NUMBER * 256U))
		die("Cezanne DMA: authoritative PCI quiescence proof failed");
	if (amd_iommu_dma_replace(&iommu_io, device_table, device_table_bytes,
		page_tables, page_table_bytes, dma_requesters, requester_count) != CB_SUCCESS)
		die("Cezanne DMA: IOMMU replacement rejected before transition");
	backend_ready = true;
	printk(BIOS_INFO,
	       "Cezanne DMA: default-deny active for %zu boot requesters; ECAM BMEs clear\n",
	       requester_count);
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, cezanne_dma_enable, NULL);

static bool prh_contains(void *unused, uint16_t segment, uint16_t bdf)
{
	(void)unused;
	return payload_resource_revision4_boot_requester(segment, bdf);
}

bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	const uint64_t generation = payload_resource_revision4_generation();
	size_t written;

	if (!address || !bytes || !backend_ready ||
	    !cezanne_dma_prh_matches(generation,
		payload_resource_revision4_published(),
		payload_resource_revision4_boot_count(), dma_requesters,
		requester_count, prh_contains, NULL) ||
	    !amd_iommu_dma_active(&iommu_io, device_table, device_table_bytes,
		page_tables, page_table_bytes, dma_requesters, requester_count))
		return false;
	for (size_t index = 0; index < requester_count; index++) {
		public_requesters[index] = (struct dma_handoff_requester) {
			.segment = 0,
			.bdf = dma_requesters[index].device_id,
			.protection_domain = dma_requesters[index].protection_domain,
			.flags = DMA_HANDOFF_REQUESTER_FLAGS,
			.arena_cpu_base = dma_requesters[index].arena_cpu_base,
			.arena_device_base = dma_requesters[index].arena_device_base,
			.arena_pages = dma_requesters[index].arena_pages,
			.arena_flags = DMA_HANDOFF_ARENA_FLAGS,
		};
	}
	if (dma_handoff_build(handoff_blob, handoff_bytes, generation,
		public_requesters, requester_count, &written) != CB_SUCCESS ||
	    written != handoff_bytes)
		return false;
	*address = (uintptr_t)handoff_blob;
	*bytes = written;
	return true;
}
