/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <boot/dma_handoff.h>
#include <bootstate.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <crc_byte.h>
#include <device/device.h>
#include <device/mmio.h>
#include <intelblocks/vtd.h>
#include <soc/vtd.h>

#include "dma_live.h"
#include "payload_resource_policy.h"
#include "../../../../../soc/intel/common/block/vtd/vtd_transition.h"

#define VTD_GLOBAL_STATUS 0x1cU
#define VTD_ROOT_ADDRESS 0x20U
#define VTD_ROOT_POINTER_SET (1U << 30)
#define VTD_TRANSLATION_ENABLE (1U << 31)

_Static_assert(STARBOOK_MTL_DMA_LIVE_MAX_FUNCTIONS ==
	LB_PRH_PCI_TOPOLOGY_MAX_ENTRIES,
	"DMA inventory and PRH topology limits must match");

struct live_context {
	uintptr_t vtd_base;
};

static struct starbook_mtl_dma_live_identity requester_identity[
	STARBOOK_MTL_DMA_LIVE_REQUESTERS];
static const struct starbook_mtl_dma_live_layout *dma_layout;
static bool backend_ready;
static bool tables_committed;
static uint32_t resident_table_crc;

static uint32_t pci_read32(void *unused, uint8_t bus, uint8_t devfn,
	uint16_t offset)
{
	const uintptr_t address = CONFIG_ECAM_MMCONF_BASE_ADDRESS +
		((uintptr_t)bus << 20) + ((uintptr_t)devfn << 12) + offset;

	(void)unused;
	return read32p(address);
}

static void pci_write16(void *unused, uint8_t bus, uint8_t devfn,
	uint16_t offset, uint16_t value)
{
	const uintptr_t address = CONFIG_ECAM_MMCONF_BASE_ADDRESS +
		((uintptr_t)bus << 20) + ((uintptr_t)devfn << 12) + offset;

	(void)unused;
	write16p(address, value);
}

static uint32_t engine_read32(void *context, uint32_t offset)
{
	return read32p(((const struct live_context *)context)->vtd_base + offset);
}

static void engine_write32(void *context, uint32_t offset, uint32_t value)
{
	write32p(((const struct live_context *)context)->vtd_base + offset, value);
}

static void commit_tables(void *unused)
{
	(void)unused;
	asm volatile("mfence" ::: "memory");
	tables_committed = true;
}

static uint32_t table_crc(void)
{
	const uint8_t *bytes = dma_layout->table_memory;
	const size_t size = dma_layout->table_used_pages *
		(1U << DMA_HANDOFF_GRANULE_SHIFT);
	uint32_t crc = 0;

	for (size_t index = 0; index < size; index++)
		crc = crc32_byte(crc, bytes[index]);
	return crc;
}

static bool engine_state_valid(const struct vtd_transition_io *transition)
{
	struct vtd_transition_facts facts;

	return dma_layout && !vtd_transition_probe(transition, &facts) &&
		facts.coherent &&
		(facts.status &
		 (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) ==
			(VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE) &&
		facts.root == dma_layout->table_physical &&
		!(facts.protected_memory_enable & (PMEN_EPM | PMEN_PRS)) &&
		tables_committed && dma_layout->table_used_pages &&
		table_crc() == resident_table_crc;
}

static void collect_requester_identity(void)
{
	static const enum starbook_mtl_boot_controller kinds[] = {
		STARBOOK_MTL_BOOT_CONTROLLER_NVME,
		STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI,
		STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI,
	};

	if (ARRAY_SIZE(kinds) != STARBOOK_MTL_DMA_LIVE_REQUESTERS ||
	    !starbook_mtl_boot_controller_inventory())
		die("StarBook MTL DMA: exact requester inventory unavailable");
	for (size_t index = 0; index < ARRAY_SIZE(kinds); index++) {
		const struct device *device =
			starbook_mtl_boot_controller_device(kinds[index]);

		if (!device || !device->upstream ||
		    device->upstream->segment_group != 0 ||
		    device->upstream->secondary > UINT8_MAX ||
		    device->path.pci.devfn > UINT8_MAX ||
		    device->vendor > UINT16_MAX || device->device > UINT16_MAX ||
		    device->class > 0xffffffU)
			die("StarBook MTL DMA: requester identity is not representable");
		requester_identity[index] =
			(struct starbook_mtl_dma_live_identity) {
				.bdf = (uint16_t)device->upstream->secondary << 8 |
					device->path.pci.devfn,
				.vendor = device->vendor,
				.device = device->device,
				.class = device->class,
			};
	}
}

static void starbook_mtl_dma_enable(void *unused)
{
	struct live_context context = { .vtd_base = soc_vtd_iop_base() };
	const struct starbook_mtl_dma_live_pci_io pci_io = {
		.read32 = pci_read32,
		.write16 = pci_write16,
	};
	const struct vtd_transition_io transition = {
		.context = &context,
		.read32 = engine_read32,
		.write32 = engine_write32,
		.commit_tables = commit_tables,
	};
	struct vtd_transition_facts facts;
	const uintptr_t ecam_base = CONFIG_ECAM_MMCONF_BASE_ADDRESS;
	const uint64_t ecam_bytes =
		(uint64_t)CONFIG_ECAM_MMCONF_BUS_NUMBER << 20;
	size_t dma_size;
	void *dma_buffer;
	uint16_t bus_count;
	int result;

	(void)unused;
	if (!context.vtd_base || (context.vtd_base & 0xfffU) ||
	    context.vtd_base > (uintptr_t)-1 - 0x1000U)
		die("StarBook MTL DMA: VTVC0 aperture is not representable");
	if (!CONFIG_ECAM_MMCONF_BUS_NUMBER ||
	    CONFIG_ECAM_MMCONF_BUS_NUMBER > 256U ||
	    (ecam_base & ((1U << 20) - 1U)) ||
	    ecam_bytes > CONFIG_ECAM_MMCONF_LENGTH ||
	    ecam_bytes - 1U > (uintptr_t)-1 - ecam_base)
		die("StarBook MTL DMA: ECAM aperture is not representable");
	bus_count = CONFIG_ECAM_MMCONF_BUS_NUMBER;
	collect_requester_identity();
	if (vtd_transition_probe(&transition, &facts) || !facts.coherent ||
	    (facts.status & (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) ||
	    (facts.protected_memory_enable & (PMEN_EPM | PMEN_PRS)) !=
		(PMEN_EPM | PMEN_PRS) || !(facts.capability & CAP_PMR_LO))
		die("StarBook MTL DMA: VTVC0 is not ready for a protected transition");
	dma_buffer = vtd_get_dma_buffer(&dma_size);
	if (!dma_buffer || !dma_size || vtd_read32(context.vtd_base, PLMBASE_REG) ||
	    vtd_read32(context.vtd_base, PLMLIMIT_REG) == UINT32_MAX ||
	    (uint64_t)vtd_read32(context.vtd_base, PLMLIMIT_REG) + 1U !=
		(uintptr_t)dma_buffer)
		die("StarBook MTL DMA: FSP buffer does not match the active low PMR");
	result = starbook_mtl_dma_live_establish(&pci_io, bus_count,
		requester_identity, dma_buffer, (uintptr_t)dma_buffer, dma_size,
		&transition);
	if (result)
		die("StarBook MTL DMA: protected transaction failed: %d", result);
	dma_layout = starbook_mtl_dma_live_layout();
	if (!dma_layout)
		die("StarBook MTL DMA: protected layout unavailable");
	resident_table_crc = table_crc();
	if (!engine_state_valid(&transition) ||
	    !starbook_mtl_dma_live_verify_active(&pci_io, bus_count))
		die("StarBook MTL DMA: protected state failed final read-back");
	backend_ready = true;
	printk(BIOS_INFO,
	       "StarBook MTL DMA: default-deny active; complete ECAM BME-clear, %zu/%zu table pages, three %u-page arenas\n",
	       dma_layout->table_used_pages, dma_layout->table_capacity_pages,
	       dma_layout->arena_pages[0]);
}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, starbook_mtl_dma_enable, NULL);

bool payload_dma_handoff_blob(uintptr_t *address, size_t *bytes)
{
	struct live_context context = { .vtd_base = soc_vtd_iop_base() };
	const struct starbook_mtl_dma_live_pci_io pci_io = {
		.read32 = pci_read32,
		.write16 = pci_write16,
	};
	const struct vtd_transition_io transition = {
		.context = &context,
		.read32 = engine_read32,
		.write32 = engine_write32,
		.commit_tables = commit_tables,
	};
	struct dma_handoff_requester requesters[STARBOOK_MTL_DMA_LIVE_REQUESTERS];
	const uint64_t generation = payload_resource_revision4_generation();
	size_t written;

	if (!address || !bytes || !backend_ready ||
	    !payload_resource_revision4_published() || !generation ||
	    payload_resource_revision4_boot_count() !=
		STARBOOK_MTL_DMA_LIVE_REQUESTERS ||
	    !engine_state_valid(&transition) ||
	    !starbook_mtl_dma_live_verify_active(&pci_io,
		CONFIG_ECAM_MMCONF_BUS_NUMBER))
		return false;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		if (!payload_resource_revision4_boot_requester(0,
			requester_identity[index].bdf))
			return false;
	if (!starbook_mtl_dma_live_handoff_requesters(requesters))
		return false;
	if (dma_handoff_build(dma_layout->handoff, dma_layout->handoff_capacity,
		generation, requesters, ARRAY_SIZE(requesters), &written) != CB_SUCCESS)
		return false;
	*address = (uintptr_t)dma_layout->handoff;
	*bytes = written;
	return true;
}
