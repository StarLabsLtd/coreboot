/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#include <boot/dma_handoff.h>
#include <commonlib/helpers.h>
#include <commonlib/bsd/cb_err.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_loader_identity.h>
#include <device/device.h>
#include <device/mmio.h>
#include <intelblocks/vtd.h>
#include <intelblocks/systemagent.h>
#include <soc/vtd.h>
#include <soc/iomap.h>
#include <soc/pci_devs.h>
#include <soc/systemagent.h>
#include <random.h>
#include <string.h>

#if CONFIG(STARLABS_STARBOOK_MTL_MOR_DMA_GUARD)
#include "dma_guard.h"
#endif
#include "dma_live.h"
#include "dma_live_mirror.h"
#include "dma_live_platform.h"
#include "loader_instance_authority.h"
#include "payload_resource_policy.h"
#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
#include "dma_smm_receipt_provision.h"
#endif
#include "../../../../../soc/intel/common/block/vtd/vtd_transition.h"

#define VTD_GLOBAL_STATUS 0x1cU
#define VTD_ROOT_ADDRESS 0x20U
#define VTD_ROOT_POINTER_SET (1U << 30)
#define VTD_TRANSLATION_ENABLE (1U << 31)

#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
static bool receipt_range_valid(const struct smm_dma_receipt_range *range)
{
	return range->base && range->size && range->base <= UINT32_MAX &&
		range->size - 1U <= UINT32_MAX - range->base;
}

static bool receipt_ranges_overlap(const struct smm_dma_receipt_range *first,
	const struct smm_dma_receipt_range *second)
{
	return first->base <= second->base ?
		second->base - first->base < first->size :
		first->base - second->base < second->size;
}

bool platform_smm_dma_receipt_memory(struct smm_dma_receipt_memory *memory)
{
	const uintptr_t vtd_base = soc_vtd_iop_base();
	const struct cbmem_entry *mirror_entry;
	uintptr_t frame_base;
	size_t frame_size;
	size_t dma_size;
	size_t mirror_size;
	void *dma = vtd_get_dma_buffer(&dma_size);
	void *mirror;
	struct smm_dma_receipt_memory value;
	uint32_t lifecycle;

	if (!memory || mainboard_loader_instance_authority_lifecycle(&lifecycle) !=
		CB_SUCCESS ||
	    !platform_smm_dma_receipt_frame(&frame_base, &frame_size) ||
	    !dma || !dma_size || !vtd_base ||
	    (vtd_read32(vtd_base, PMEN_REG) & (PMEN_EPM | PMEN_PRS)) !=
		(PMEN_EPM | PMEN_PRS) || vtd_read32(vtd_base, PLMBASE_REG) ||
	    vtd_read32(vtd_base, PLMLIMIT_REG) == UINT32_MAX ||
	    (uint64_t)vtd_read32(vtd_base, PLMLIMIT_REG) + 1U !=
		(uintptr_t)dma ||
	    starbook_mtl_dma_live_table_mirror_size((uintptr_t)dma, dma_size,
		&mirror_size))
		return false;
	mirror = starbook_mtl_dma_live_mirror_acquire(mirror_size, lifecycle);
	mirror_entry = cbmem_entry_find(CBMEM_ID_MTL_DMA_MIRROR);
	value = (struct smm_dma_receipt_memory) {
		.revision = SMM_DMA_RECEIPT_MEMORY_REVISION,
		.size = sizeof(value),
		.frame = { frame_base, frame_size },
		.dma = { (uintptr_t)dma, dma_size },
		.mirror = { (uintptr_t)mirror, mirror_size },
	};
	if (!mirror || !mirror_entry || cbmem_entry_start(mirror_entry) != mirror ||
	    cbmem_entry_size(mirror_entry) != mirror_size ||
	    (uintptr_t)mirror > (uintptr_t)-1 - mirror_size ||
	    (uintptr_t)mirror + mirror_size > (uintptr_t)dma ||
	    !receipt_range_valid(&value.frame) ||
	    !receipt_range_valid(&value.dma) ||
	    !receipt_range_valid(&value.mirror) ||
	    receipt_ranges_overlap(&value.frame, &value.dma) ||
	    receipt_ranges_overlap(&value.frame, &value.mirror) ||
	    receipt_ranges_overlap(&value.dma, &value.mirror))
		return false;
	*memory = value;
	return true;
}
#endif

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
static bool backend_poisoned;
static bool tables_committed;

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

static void poison_backend(const struct pci_bme_quiesce_io *pci_io)
{
	starbook_mtl_dma_live_poison(pci_io,
		CONFIG_ECAM_MMCONF_BUS_NUMBER);
	backend_poisoned = true;
	backend_ready = false;
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

static bool engine_state_valid(const struct vtd_transition_io *transition,
	struct vtd_transition_facts *observed)
{
	struct vtd_transition_facts facts;

	if (!starbook_mtl_dma_live_tables_match(dma_layout))
		return false;
	if (vtd_transition_probe(transition, &facts) ||
	    !facts.coherent ||
	    (facts.status & (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) !=
		(VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE) ||
	    facts.root != dma_layout->table_physical ||
	    (facts.protected_memory_enable & (PMEN_EPM | PMEN_PRS)) ||
	    !tables_committed || !starbook_mtl_dma_live_tables_match(dma_layout))
		return false;
	if (observed)
		*observed = facts;
	return true;
}

static bool collect_requester_identity(void)
{
	static const enum starbook_mtl_boot_controller kinds[] = {
		STARBOOK_MTL_BOOT_CONTROLLER_NVME,
		STARBOOK_MTL_BOOT_CONTROLLER_PCH_XHCI,
		STARBOOK_MTL_BOOT_CONTROLLER_TCSS_XHCI,
	};

	if (ARRAY_SIZE(kinds) != STARBOOK_MTL_DMA_LIVE_REQUESTERS ||
	    !starbook_mtl_boot_controller_inventory())
		return false;
	for (size_t index = 0; index < ARRAY_SIZE(kinds); index++) {
		const struct device *device =
			starbook_mtl_boot_controller_device(kinds[index]);

		if (!device || !device->upstream ||
		    device->upstream->segment_group != 0 ||
		    device->upstream->secondary > UINT8_MAX ||
		    device->path.pci.devfn > UINT8_MAX ||
		    device->vendor > UINT16_MAX || device->device > UINT16_MAX ||
		    device->class > 0xffffffU)
			return false;
		requester_identity[index] =
			(struct starbook_mtl_dma_live_identity) {
				.bdf = (uint16_t)device->upstream->secondary << 8 |
					device->path.pci.devfn,
				.vendor = device->vendor,
				.device = device->device,
				.class = device->class,
			};
	}
	return true;
}

enum cb_err starbook_mtl_dma_live_backend_ensure(void)
{
	struct live_context context = { .vtd_base = soc_vtd_iop_base() };
	const struct pci_bme_quiesce_io pci_io = {
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
	size_t mirror_size;
	void *dma_buffer;
	void *table_mirror;
	const struct cbmem_entry *mirror_entry;
	uint16_t bus_count;
	bool engine_valid;
	bool pci_valid;
	int result;
#if CONFIG(STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY)
	uint32_t lifecycle;
#endif

	if (backend_poisoned)
		return CB_ERR;
	if (backend_ready) {
		if (starbook_mtl_dma_live_verify_active(&pci_io,
			CONFIG_ECAM_MMCONF_BUS_NUMBER) &&
		    engine_state_valid(&transition, NULL))
			return CB_SUCCESS;
		poison_backend(&pci_io);
		return CB_ERR;
	}
	/* Establishment is terminal: every later return keeps this poisoned. */
	backend_poisoned = true;
	if (!context.vtd_base || (context.vtd_base & 0xfffU) ||
	    context.vtd_base > (uintptr_t)-1 - 0x1000U)
		return CB_ERR;
	if (!CONFIG_ECAM_MMCONF_BUS_NUMBER ||
	    CONFIG_ECAM_MMCONF_BUS_NUMBER > 256U ||
	    (ecam_base & ((1U << 20) - 1U)) ||
	    ecam_bytes > CONFIG_ECAM_MMCONF_LENGTH ||
	    ecam_bytes - 1U > (uintptr_t)-1 - ecam_base)
		return CB_ERR;
	bus_count = CONFIG_ECAM_MMCONF_BUS_NUMBER;
	if (!collect_requester_identity())
		return CB_ERR;
	if (vtd_transition_probe(&transition, &facts) || !facts.coherent ||
	    (facts.status & (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) ||
	    (facts.protected_memory_enable & (PMEN_EPM | PMEN_PRS)) !=
		(PMEN_EPM | PMEN_PRS) || !(facts.capability & CAP_PMR_LO))
		return CB_ERR;
	dma_buffer = vtd_get_dma_buffer(&dma_size);
	if (!dma_buffer || !dma_size || vtd_read32(context.vtd_base, PLMBASE_REG) ||
	    vtd_read32(context.vtd_base, PLMLIMIT_REG) == UINT32_MAX ||
	    (uint64_t)vtd_read32(context.vtd_base, PLMLIMIT_REG) + 1U !=
		(uintptr_t)dma_buffer)
		return CB_ERR;
	if (starbook_mtl_dma_live_table_mirror_size((uintptr_t)dma_buffer,
		dma_size, &mirror_size))
		return CB_ERR;
#if CONFIG(STARLABS_STARBOOK_MTL_LOADER_INSTANCE_AUTHORITY)
	if (mainboard_loader_instance_authority_lifecycle(&lifecycle) != CB_SUCCESS)
		return CB_ERR;
	table_mirror = starbook_mtl_dma_live_mirror_acquire(mirror_size, lifecycle);
#else
	table_mirror = cbmem_add(CBMEM_ID_MTL_DMA_MIRROR, mirror_size);
#endif
	mirror_entry = cbmem_entry_find(CBMEM_ID_MTL_DMA_MIRROR);
	if (!table_mirror || (uintptr_t)table_mirror > (uintptr_t)-1 - mirror_size ||
	    (uintptr_t)table_mirror + mirror_size > (uintptr_t)dma_buffer ||
	    !mirror_entry || cbmem_entry_start(mirror_entry) != table_mirror ||
	    cbmem_entry_size(mirror_entry) != mirror_size)
		return CB_ERR;
	result = starbook_mtl_dma_live_establish(&pci_io, bus_count,
		requester_identity, dma_buffer, (uintptr_t)dma_buffer, dma_size,
		table_mirror, (uintptr_t)table_mirror, mirror_size,
		&transition);
	if (result) {
		poison_backend(&pci_io);
		return CB_ERR;
	}
	dma_layout = starbook_mtl_dma_live_layout();
	if (!dma_layout) {
		poison_backend(&pci_io);
		return CB_ERR;
	}
	pci_valid = starbook_mtl_dma_live_verify_active(&pci_io, bus_count);
	engine_valid = engine_state_valid(&transition, NULL);

	if (!engine_valid || !pci_valid) {
		poison_backend(&pci_io);
		return CB_ERR;
	}
	backend_ready = true;
	backend_poisoned = false;
	printk(BIOS_INFO,
	       "StarBook MTL DMA: default-deny active; complete ECAM BME-clear, %zu/%zu table pages, three %u-page arenas\n",
	       dma_layout->table_used_pages, dma_layout->table_capacity_pages,
	       dma_layout->arena_pages[0]);
	return CB_SUCCESS;
}

#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
enum cb_err starbook_mtl_dma_receipt_candidate_build(
	struct starbook_mtl_dma_smm_receipt *candidate)
{
	static struct pci_bme_quiesce_snapshot snapshot;
	static struct pci_bme_quiesce_snapshot rechecked_snapshot;
	struct starbook_mtl_dma_live_layout layout;
	struct starbook_mtl_dma_live_layout rechecked_layout;
	struct live_context vtvc0_context = { .vtd_base = soc_vtd_iop_base() };
	struct live_context gfx_context = { .vtd_base = GFXVT_BASE_ADDRESS };
	const struct vtd_transition_io vtvc0 = {
		.context = &vtvc0_context,
		.read32 = engine_read32,
		.write32 = engine_write32,
		.commit_tables = commit_tables,
	};
	const struct vtd_transition_io gfx = {
		.context = &gfx_context,
		.read32 = engine_read32,
	};
	const struct pci_bme_quiesce_io pci_io = {
		.read32 = pci_read32,
		.write16 = pci_write16,
	};
	const uint16_t integrated[] = { PCI_DEVFN_IGD, PCI_DEVFN_IPU };
	struct vtd_transition_facts vtvc0_facts;
	struct vtd_transition_facts gfx_facts;
	const struct cbmem_entry *mirror_entry;
	const uint64_t gfxvtbar = MCHBAR64(GFXVTBAR);
	const uint64_t page_size = 1ULL << DMA_HANDOFF_GRANULE_SHIFT;
	uint64_t table_capacity;
	uint64_t table_used;

	if (!candidate || !backend_ready || backend_poisoned ||
	    !starbook_mtl_dma_live_snapshot(&layout, &snapshot) ||
	    !engine_state_valid(&vtvc0, &vtvc0_facts) ||
	    vtd_transition_probe(&gfx, &gfx_facts) || !gfx_facts.coherent ||
	    (gfx_facts.status &
		(VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) ||
	    (gfx_facts.protected_memory_enable & (PMEN_EPM | PMEN_PRS)) ||
	    gfxvtbar != (GFXVT_BASE_ADDRESS | VTBAR_ENABLED) ||
	    !starbook_mtl_dma_live_devices_are_verified(&pci_io,
		CONFIG_ECAM_MMCONF_BUS_NUMBER, integrated,
		ARRAY_SIZE(integrated)) ||
	    snapshot.bus_count != CONFIG_ECAM_MMCONF_BUS_NUMBER ||
	    !snapshot.count ||
	    snapshot.count > PCI_BME_QUIESCE_MAX_FUNCTIONS ||
	    layout.table_capacity_pages > UINT64_MAX /
		page_size ||
	    layout.table_used_pages > UINT64_MAX /
		page_size)
		return CB_ERR;
	table_capacity = (uint64_t)layout.table_capacity_pages <<
		DMA_HANDOFF_GRANULE_SHIFT;
	table_used = (uint64_t)layout.table_used_pages <<
		DMA_HANDOFF_GRANULE_SHIFT;
	mirror_entry = cbmem_entry_find(CBMEM_ID_MTL_DMA_MIRROR);
	if (!mirror_entry || layout.buffer !=
		(void *)(uintptr_t)layout.buffer_physical ||
	    layout.handoff != layout.buffer ||
	    layout.table_memory != (uint8_t *)layout.buffer + page_size ||
	    layout.table_physical != layout.buffer_physical + page_size ||
	    layout.table_mirror !=
		(void *)(uintptr_t)layout.table_mirror_physical ||
	    cbmem_entry_start(mirror_entry) != layout.table_mirror ||
	    cbmem_entry_size(mirror_entry) != table_capacity ||
	    !table_capacity || !table_used || table_used > table_capacity)
		return CB_ERR;
	for (size_t index = 0; index < STARBOOK_MTL_DMA_SMM_ARENAS; index++)
		if (!layout.arena_pages[index] ||
		    layout.arena_pages[index] > UINT64_MAX / page_size)
			return CB_ERR;

	*candidate = (struct starbook_mtl_dma_smm_receipt) {
		.revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION,
		.size = sizeof(*candidate),
		.bus_count = snapshot.bus_count,
		.function_count = snapshot.count,
		.gfx_mode = STARBOOK_MTL_DMA_SMM_GFX_QUIESCED,
		.ecam_base = CONFIG_ECAM_MMCONF_BASE_ADDRESS,
		.vtvc0_base = vtvc0_context.vtd_base,
		.vtvc0_rtaddr = vtvc0_facts.root,
		.gfx_base = gfx_context.vtd_base,
		.gfxvtbar_register = MCH_BASE_ADDRESS + GFXVTBAR,
		.gfxvtbar_value = gfxvtbar,
		.handoff = { layout.buffer_physical, page_size },
		.tables = { layout.table_physical, table_capacity },
		.table_mirror = { layout.table_mirror_physical,
			table_capacity },
		.table_used_bytes = table_used,
	};
	for (size_t index = 0; index < STARBOOK_MTL_DMA_SMM_ARENAS; index++)
		candidate->arenas[index] =
			(struct starbook_mtl_dma_smm_range) {
				.base = layout.arena_base[index],
				.size = (uint64_t)layout.arena_pages[index] <<
					DMA_HANDOFF_GRANULE_SHIFT,
			};
	memcpy(candidate->functions, snapshot.functions,
		snapshot.count * sizeof(snapshot.functions[0]));
	if (!starbook_mtl_dma_live_snapshot(&rechecked_layout,
		&rechecked_snapshot) ||
	    memcmp(&layout, &rechecked_layout, sizeof(layout)) ||
	    memcmp(&snapshot, &rechecked_snapshot, sizeof(snapshot)) ||
	    !engine_state_valid(&vtvc0, NULL) ||
	    !starbook_mtl_dma_live_tables_match(&layout) ||
	    !starbook_mtl_dma_live_verify_active(&pci_io,
		CONFIG_ECAM_MMCONF_BUS_NUMBER)) {
		memset(candidate, 0, sizeof(*candidate));
		return CB_ERR;
	}
	return CB_SUCCESS;
}
#endif

#if CONFIG(STARLABS_STARBOOK_MTL_MOR_DMA_GUARD)
static enum cb_err platform_observe(void *unused,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	struct starbook_mtl_dma_guard_facts observed = { 0 };
	struct live_context vtvc0_context = { .vtd_base = soc_vtd_iop_base() };
	struct live_context gfx_context = { .vtd_base = GFXVT_BASE_ADDRESS };
	const struct vtd_transition_io vtvc0 = {
		.context = &vtvc0_context,
		.read32 = engine_read32,
		.write32 = engine_write32,
		.commit_tables = commit_tables,
	};
	const struct vtd_transition_io gfx = {
		.context = &gfx_context,
		.read32 = engine_read32,
	};
	struct vtd_transition_facts vtvc0_facts;
	struct vtd_transition_facts gfx_facts;
	size_t current_dma_size;
	void *current_dma_buffer;
	const struct cbmem_entry *mirror_entry;
	const uint64_t gfxvtbar = MCHBAR64(GFXVTBAR);
	const uint16_t integrated_requesters[] = { PCI_DEVFN_IGD, PCI_DEVFN_IPU };
	const struct pci_bme_quiesce_io pci_io = {
		.read32 = pci_read32,
		.write16 = pci_write16,
	};
	bool integrated_verified;
	(void)unused;
	current_dma_buffer = vtd_get_dma_buffer(&current_dma_size);
	mirror_entry = cbmem_entry_find(CBMEM_ID_MTL_DMA_MIRROR);
	/* Always run the fail-closed full-ECAM verifier before engine checks. */
	integrated_verified = starbook_mtl_dma_live_devices_are_verified(&pci_io,
		CONFIG_ECAM_MMCONF_BUS_NUMBER, integrated_requesters,
		ARRAY_SIZE(integrated_requesters));
	if (!(gfxvtbar & VTBAR_ENABLED) ||
	    (gfxvtbar & VTBAR_MASK) != GFXVT_BASE_ADDRESS ||
	    !dma_layout || !current_dma_buffer || !mirror_entry ||
	    dma_layout->buffer != (void *)(uintptr_t)dma_layout->buffer_physical ||
	    dma_layout->handoff != dma_layout->buffer ||
	    dma_layout->table_memory != (uint8_t *)dma_layout->buffer +
		(1U << DMA_HANDOFF_GRANULE_SHIFT) ||
	    dma_layout->table_mirror !=
		(void *)(uintptr_t)dma_layout->table_mirror_physical ||
	    !engine_state_valid(&vtvc0, &vtvc0_facts) ||
	    vtd_transition_probe(&gfx, &gfx_facts) || !gfx_facts.coherent) {
		return CB_ERR;
	}
	observed = (struct starbook_mtl_dma_guard_facts) {
		.live_buffer = { dma_layout->buffer_physical,
			dma_layout->buffer_size },
		.current_fsp_buffer = { (uintptr_t)current_dma_buffer,
			current_dma_size },
		.table_mirror = { dma_layout->table_mirror_physical,
			(uint64_t)dma_layout->table_capacity_pages <<
			DMA_HANDOFF_GRANULE_SHIFT },
		.current_cbmem_mirror = { (uintptr_t)cbmem_entry_start(mirror_entry),
			cbmem_entry_size(mirror_entry) },
		.handoff = { dma_layout->table_physical -
			(1U << DMA_HANDOFF_GRANULE_SHIFT),
			1U << DMA_HANDOFF_GRANULE_SHIFT },
		.table = { dma_layout->table_physical,
			dma_layout->table_capacity_pages << DMA_HANDOFF_GRANULE_SHIFT },
		.gfxvtbar = gfxvtbar,
		.tables_match = starbook_mtl_dma_live_tables_match(dma_layout),
		.integrated_requesters_verified = integrated_verified,
	};
	observed.engines[0] = (struct starbook_mtl_dma_guard_engine) {
		.base = vtvc0_context.vtd_base, .root = vtvc0_facts.root,
		.capability = vtvc0_facts.capability,
		.extended_capability = vtvc0_facts.extended_capability,
		.version = vtvc0_facts.version, .status = vtvc0_facts.status,
		.protected_memory_enable = vtvc0_facts.protected_memory_enable,
		.mode = STARBOOK_MTL_DMA_GUARD_DEFAULT_DENY_TRANSLATION,
	};
	observed.engines[1] = (struct starbook_mtl_dma_guard_engine) {
		.base = gfx_context.vtd_base, .root = gfx_facts.root,
		.capability = gfx_facts.capability,
		.extended_capability = gfx_facts.extended_capability,
		.version = gfx_facts.version, .status = gfx_facts.status,
		.protected_memory_enable = gfx_facts.protected_memory_enable,
		.mode = STARBOOK_MTL_DMA_GUARD_BME_QUIESCED,
	};
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		observed.arenas[index] = (struct starbook_mtl_dma_guard_range) {
			.base = dma_layout->arena_base[index],
			.size = (uint64_t)dma_layout->arena_pages[index] <<
				DMA_HANDOFF_GRANULE_SHIFT,
		};
	return starbook_mtl_dma_guard_snapshot_build(&observed, snapshot);
}

static enum cb_err platform_ensure(void *unused)
{
	(void)unused;
	return starbook_mtl_dma_live_backend_ensure();
}

static enum cb_err platform_random64(void *unused, uint64_t *value)
{
	(void)unused;
	return get_random_number_64(value);
}

static void platform_poison(void *unused)
{
	const struct pci_bme_quiesce_io pci_io = {
		.read32 = pci_read32,
		.write16 = pci_write16,
	};

	(void)unused;
	poison_backend(&pci_io);
}

static const struct starbook_mtl_dma_guard_ops platform_guard_ops = {
	.ensure = platform_ensure,
	.observe = platform_observe,
	.random64 = platform_random64,
	.poison = platform_poison,
};

enum cb_err starbook_mtl_dma_guard_prepare(
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	return starbook_mtl_dma_guard_prepare_with_ops(snapshot,
		&platform_guard_ops);
}

#if ENV_TEST
enum cb_err starbook_mtl_dma_guard_bind(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct starbook_mtl_dma_guard_snapshot *bound,
	struct payload_mm_authvar_mor_clear_dma_snapshot *dma)
{
	return starbook_mtl_dma_guard_bind_with_ops(plan, prepared, bound, dma,
		&platform_guard_ops);
}
#endif

enum cb_err starbook_mtl_dma_guard_bind_owned(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *prepared,
	struct starbook_mtl_dma_guard_snapshot *bound,
	struct payload_mm_authvar_mor_clear_dma_snapshot *dma,
	struct starbook_mtl_dma_guard_bind_workspace *workspace)
{
	return starbook_mtl_dma_guard_bind_with_ops_owned(plan, prepared, bound, dma,
		&platform_guard_ops, workspace);
}
#endif

#if CONFIG(STARLABS_STARBOOK_MTL_DMA_HANDOFF)
bool starbook_mtl_dma_live_backend_handoff(uintptr_t *address, size_t *bytes)
{
	struct live_context context = { .vtd_base = soc_vtd_iop_base() };
	const struct pci_bme_quiesce_io pci_io = {
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
	bool engine_valid;
	bool pci_valid;
	size_t written;

	if (!backend_ready)
		return false;
	pci_valid = starbook_mtl_dma_live_verify_active(&pci_io,
		CONFIG_ECAM_MMCONF_BUS_NUMBER);
	engine_valid = engine_state_valid(&transition, NULL);
	if (!engine_valid || !pci_valid) {
		poison_backend(&pci_io);
		return false;
	}
	if (!address || !bytes ||
	    !payload_resource_revision4_published() || !generation ||
	    payload_resource_revision4_boot_count() !=
		STARBOOK_MTL_DMA_LIVE_REQUESTERS)
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
#endif
