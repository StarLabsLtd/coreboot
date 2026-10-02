/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/pci_io_cfg.h>
#include <boot/capsule_broker_buffers.h>
#include <boot/payload_mm_authvar_service_receiver.h>
#include <bootstate.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/cache.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <delay.h>
#include <device/mmio.h>
#include <device/pci_def.h>
#include <string.h>
#if ENV_RAMSTAGE
#include <device/device.h>
#endif

#include "q35.h"
#include "q35_dma_cold.h"
#include "q35_dma_controller_cancel.h"
#include "q35_dma_table_image.h"
#include "vtd_registers.h"
#if ENV_SMM
#include "native_service.h"
#endif

#define VTD_BASE 0xfed90000U
#define PCI_FUNCTIONS 8U
#define CONTROLLERS 3U
#define PCI_BARS 6U
#define PAGE_SIZE 4096U

/* No bridges, optional requesters, or arbitrary controller reset callbacks. */
static const struct {
	uint16_t bdf;
	uint16_t vendor;
	uint16_t device;
} topology[PCI_FUNCTIONS] = {
	{0x00, 0x8086, 0x29c0},
	{0x08, 0x1234, 0x1111},
	{0x18, 0x1b36, 0x0010},
	{0x20, 0x1b36, 0x000d},
	{0x28, 0x1234, 0x11e8},
	{0xf8, 0x8086, 0x2918},
	{0xfa, 0x8086, 0x2922},
	{0xfb, 0x8086, 0x2930},
};

struct controller_aperture {
	uintptr_t base;
	size_t size;
	bool fault;
};

enum dma_wave { DMA_PRIVATE_FINALIZE, DMA_CAPSULE_RAM };

static bool wave_current(enum dma_wave wave)
{
#if ENV_SMM
	if (wave == DMA_PRIVATE_FINALIZE)
		return platform_payload_mm_authvar_service_finalize_admitted();
	if (wave == DMA_CAPSULE_RAM)
		return q35_capsule_ram_transaction_current();
	return false;
#else
	return wave == DMA_PRIVATE_FINALIZE;
#endif
}

static bool span_valid(uint64_t base, uint64_t size)
{
	return base && size && base <= UINT32_MAX && size - 1U <= UINT32_MAX - base;
}

static bool overlap(uint64_t left, uint64_t left_size, uint64_t right, uint64_t right_size)
{
	return left < right + right_size && right < left + left_size;
}

static uint32_t pciexbar_expected(void)
{
	uint32_t length;

	switch (CONFIG_ECAM_MMCONF_BUS_NUMBER) {
	case 256:
		length = 0;
		break;
	case 128:
		length = 2;
		break;
	case 64:
		length = 4;
		break;
	default:
		return 0;
	}
	return CONFIG_ECAM_MMCONF_BASE_ADDRESS | length | 1U;
}

static bool ecam_current(void)
{
	const uint32_t expected = pciexbar_expected();

	/* Use config IO before accepting the physical ECAM pointer. */
	return expected && pci_io_read_config32(HOST_BRIDGE, PCI_VENDOR_ID) == 0x29c08086U &&
		pci_io_read_config32(HOST_BRIDGE, D0F0_PCIEXBAR_LO) == expected &&
		!pci_io_read_config32(HOST_BRIDGE, D0F0_PCIEXBAR_HI);
}

static uintptr_t pci_config(uint16_t bdf)
{
	return CONFIG_ECAM_MMCONF_BASE_ADDRESS + ((uintptr_t)bdf << 12);
}

static bool device_translation_absent(uintptr_t config)
{
	uint32_t offset = PCIE_EXT_CAP_OFFSET;

	/* Only checked PCI config space, never a table or guest pointer walk. */
	for (uint32_t visited = 0; visited < (4096U - PCIE_EXT_CAP_OFFSET) / 4U; visited++) {
		const uint32_t header = read32((void *)(config + offset));
		const uint32_t capability = PCI_EXT_CAP_ID(header);
		const uint32_t next = header >> 20;

		if ((!header || header == UINT32_MAX) && offset == PCIE_EXT_CAP_OFFSET)
			return true;
		if (!capability || !PCI_EXT_CAP_VER(header) || header == UINT32_MAX ||
		    capability == PCIE_EXT_CAP_ID_ATS || capability == 0x13U || capability == 0x1bU)
			return false;
		if (!next)
			return true;
		if (next < PCIE_EXT_CAP_OFFSET || next > 4092U || (next & 3U))
			return false;
		offset = next;
	}
	return false;
}

static bool inventory(bool clear_master)
{
	size_t count = 0;
	bool valid = ecam_current();

	if (!valid)
		return false;
	/* Quiesce every present function even after encountering a mismatch. */
	for (uint32_t bus = 0; bus < CONFIG_ECAM_MMCONF_BUS_NUMBER; bus++) {
		for (uint32_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
			const uint16_t bdf = (uint16_t)((bus << 8) | devfn);
			const uintptr_t config = pci_config(bdf);
			const uint32_t identity = read32((void *)(config + PCI_VENDOR_ID));
			uint16_t command;

			if ((uint16_t)identity == UINT16_MAX)
				continue;
			if (count >= ARRAY_SIZE(topology) || topology[count].bdf != bdf ||
			    identity != ((uint32_t)topology[count].device << 16 | topology[count].vendor) ||
			    (read8((void *)(config + PCI_HEADER_TYPE)) & 0x7fU) != PCI_HEADER_TYPE_NORMAL ||
			    !device_translation_absent(config))
				valid = false;
			command = read16((void *)(config + PCI_COMMAND));
			if (clear_master)
				write16((void *)(config + PCI_COMMAND), command & ~PCI_COMMAND_MASTER);
			if (read16((void *)(config + PCI_COMMAND)) & PCI_COMMAND_MASTER)
				valid = false;
			count++;
		}
	}
	return valid && count == ARRAY_SIZE(topology) && ecam_current();
}

static bool smram_geometry(uintptr_t *base, size_t *size)
{
	const uint8_t esmram = pci_io_read_config8(HOST_BRIDGE, ESMRAMC);
	const uint8_t smram = pci_io_read_config8(HOST_BRIDGE, SMRAMC);
	const size_t decoded = (esmram & TSEG_SZ_MASK) == 0 ? MiB :
		(esmram & TSEG_SZ_MASK) == 2 ? 2U * MiB :
		(esmram & TSEG_SZ_MASK) == 4 ? 8U * MiB : 0;

	smm_region(base, size);
	if (!span_valid(*base, *size) || *size != CONFIG_SMM_TSEG_SIZE || decoded != *size)
		return false;
#if ENV_SMM
	const struct smm_invocation_runtime_view *view;
	uint32_t cpus;

	if (!(esmram & T_EN) ||
	    (smram & (D_LCK | G_SMRAME | D_OPEN)) != (D_LCK | G_SMRAME) ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_cpu_count(view, &cpus) != CB_SUCCESS || cpus != 1 ||
	    smm_invocation_runtime_geometry_is_contained(view, *base, *size) != CB_SUCCESS)
		return false;
#else
	(void)smram;
#endif
	return true;
}

static bool mmio_span(uint64_t base, uint64_t size, uintptr_t smram_base, size_t smram_size)
{
	const uint64_t ecam_size = (uint64_t)CONFIG_ECAM_MMCONF_BUS_NUMBER << 20;
	const uint64_t flash = 0x100000000ULL - CONFIG_ROM_SIZE;

	return span_valid(base, size) && !(size & (size - 1U)) && !(base & (size - 1U)) &&
		base >= (uint64_t)smram_base + smram_size &&
		!overlap(base, size, CONFIG_ECAM_MMCONF_BASE_ADDRESS, ecam_size) &&
		!overlap(base, size, VTD_BASE, PAGE_SIZE) &&
		!overlap(base, size, 0xfec00000U, 0x300000U) &&
		!overlap(base, size, flash, CONFIG_ROM_SIZE);
}

static const struct resource *bar_resource(const struct smm_pci_resource_info *slot,
	uint32_t index)
{
	const struct resource *found = NULL;

	for (size_t i = 0; i < ARRAY_SIZE(slot->resources); i++) {
		const struct resource *resource = &slot->resources[i];

		if (resource->index != index || !resource->flags)
			continue;
		if (found)
			return NULL;
		found = resource;
	}
	return found;
}

static bool resource_image_valid(const struct smm_pci_resource_info slots[PCI_FUNCTIONS],
	uintptr_t smram_base, size_t smram_size)
{
	struct controller_aperture apertures[PCI_FUNCTIONS * PCI_BARS];
	size_t aperture_count = 0;

	for (size_t device = 0; device < PCI_FUNCTIONS; device++) {
		const struct smm_pci_resource_info *slot = &slots[device];
		const uintptr_t config = pci_config(topology[device].bdf);

		if (slot->pci_addr != PCI_DEV(0, topology[device].bdf >> 3, topology[device].bdf & 7U) ||
		    slot->vendor_id != topology[device].vendor || slot->device_id != topology[device].device ||
		    slot->class_device != read16((void *)(config + PCI_CLASS_DEVICE)) ||
		    slot->class_prog != read8((void *)(config + PCI_CLASS_PROG)))
			return false;
		for (uint32_t bar = 0; bar < PCI_BARS; bar++) {
			const uint32_t index = PCI_BASE_ADDRESS_0 + bar * 4U;
			const uint32_t low = read32((void *)(config + index));
			const struct resource *resource = bar_resource(slot, index);
			const bool pci64 = (low & PCI_BASE_ADDRESS_MEM_LIMIT_MASK) == PCI_BASE_ADDRESS_MEM_LIMIT_64;
			uint64_t base;

			if (low & PCI_BASE_ADDRESS_SPACE_IO)
				continue;
			base = low & ~PCI_BASE_ADDRESS_MEM_ATTR_MASK;
			if (pci64) {
				if (bar == PCI_BARS - 1U)
					return false;
				base |= (uint64_t)read32((void *)(config + index + 4U)) << 32;
				bar++;
			}
			if (!base && !resource)
				continue;
			if (!resource || (low & PCI_BASE_ADDRESS_MEM_LIMIT_MASK) !=
			    (pci64 ? PCI_BASE_ADDRESS_MEM_LIMIT_64 : PCI_BASE_ADDRESS_MEM_LIMIT_32) ||
			    (resource->flags & (IORESOURCE_TYPE_MASK | IORESOURCE_ASSIGNED | IORESOURCE_STORED |
				IORESOURCE_PCI64)) != (IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_STORED |
				(pci64 ? IORESOURCE_PCI64 : 0U)) || resource->base != base ||
			    !mmio_span(base, resource->size, smram_base, smram_size))
				return false;
			for (size_t previous = 0; previous < aperture_count; previous++)
				if (overlap(base, resource->size, apertures[previous].base, apertures[previous].size))
					return false;
			apertures[aperture_count++] = (struct controller_aperture) {
				.base = base, .size = resource->size,
			};
		}
	}
	return true;
}

static uint32_t controller_read(void *context, uint32_t offset)
{
	struct controller_aperture *aperture = context;

	if ((offset & 3U) || offset > aperture->size || sizeof(uint32_t) > aperture->size - offset) {
		aperture->fault = true;
		return UINT32_MAX;
	}
	return read32((void *)(aperture->base + offset));
}

static void controller_write(void *context, uint32_t offset, uint32_t value)
{
	struct controller_aperture *aperture = context;

	if ((offset & 3U) || offset > aperture->size || sizeof(uint32_t) > aperture->size - offset) {
		aperture->fault = true;
		return;
	}
	write32((void *)(aperture->base + offset), value);
}

static void controller_delay(void *unused, uint32_t microseconds)
{
	(void)unused;
	udelay(microseconds);
}

static bool controllers_quiesce(const struct smm_pci_resource_info slots[PCI_FUNCTIONS])
{
	static const size_t devices[CONTROLLERS] = {2, 3, 6};
	static const uint32_t bars[CONTROLLERS] = {PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_5};
	bool (*const cancel[CONTROLLERS])(const struct q35_dma_controller_io *) = {
		q35_dma_nvme_cancel, q35_dma_xhci_cancel, q35_dma_ahci_cancel,
	};
	const struct resource *edu = bar_resource(&slots[4], PCI_BASE_ADDRESS_0);
	bool retired = true;

	for (size_t i = 0; i < CONTROLLERS; i++) {
		const struct resource *resource = bar_resource(&slots[devices[i]], bars[i]);
		struct controller_aperture aperture;
		struct q35_dma_controller_io io;

		if (!resource || resource->size < PAGE_SIZE ||
		    !(read16((void *)(pci_config(topology[devices[i]].bdf) + PCI_COMMAND)) & PCI_COMMAND_MEMORY))
			return false;
		aperture = (struct controller_aperture) {.base = resource->base, .size = resource->size};
		io = (struct q35_dma_controller_io) {
			.context = &aperture, .read32 = controller_read,
			.write32 = controller_write, .delay_us = controller_delay,
		};
		if (!cancel[i](&io) || aperture.fault)
			retired = false;
	}
	/* EDU holds no asynchronous host mapping: wait for its synchronous timer. */
	if (!edu || edu->size < 0xa0U ||
	    !(read16((void *)(pci_config(topology[4].bdf) + PCI_COMMAND)) & PCI_COMMAND_MEMORY))
		return false;
	for (uint32_t retry = 0; retry < 100000U; retry++) {
		const uint64_t command = read64((void *)(uintptr_t)(edu->base + 0x98U));

		if (command == UINT64_MAX)
			return false;
		if (!(command & 1U))
			return retired;
		udelay(10U);
	}
	return false;
}

static bool resource_image(struct smm_pci_resource_info slots[PCI_FUNCTIONS])
{
#if ENV_RAMSTAGE
	const struct device *devices[PCI_FUNCTIONS];

	for (size_t i = 0; i < PCI_FUNCTIONS; i++) {
		const struct device *device = pcidev_on_root(topology[i].bdf >> 3, topology[i].bdf & 7U);

		if (!device || !device->enabled || !is_pci(device) || device->vendor != topology[i].vendor ||
		    device->device != topology[i].device)
			return false;
		devices[i] = device;
	}
	memset(slots, 0, PCI_FUNCTIONS * sizeof(*slots));
	return smm_pci_resource_store_fill_resources(slots, PCI_FUNCTIONS, devices, PCI_FUNCTIONS);
#else
	const volatile struct smm_pci_resource_info *stored = smm_get_pci_resource_store();
	const struct smm_invocation_runtime_view *view;

	if (CONFIG_SMM_PCI_RESOURCE_STORE_NUM_SLOTS != PCI_FUNCTIONS || !stored ||
	    smm_invocation_runtime_view_get(&view) != CB_SUCCESS ||
	    smm_invocation_runtime_range_is_protected(view, (const void *)stored,
		PCI_FUNCTIONS * sizeof(*slots)) != CB_SUCCESS)
		return false;
	for (size_t i = 0; i < PCI_FUNCTIONS; i++)
		slots[i] = stored[i];
	return true;
#endif
}

static bool controllers_retired(enum dma_wave wave)
{
	struct smm_pci_resource_info slots[PCI_FUNCTIONS];
	uintptr_t smram_base;
	size_t smram_size;
	const uint32_t config_index = inl(PCI_IO_CONFIG_INDEX);
	bool valid = false;

	if (!wave_current(wave))
		goto out;
	if (!inventory(true) || !smram_geometry(&smram_base, &smram_size) ||
	    !resource_image(slots) || !resource_image_valid(slots, smram_base, smram_size) ||
	    !controllers_quiesce(slots) || !inventory(false) ||
	    !resource_image_valid(slots, smram_base, smram_size))
		goto out;
	if (!wave_current(wave))
		goto out;
	valid = true;
out:
	outl(config_index, PCI_IO_CONFIG_INDEX);
	return valid;
}

bool q35_dma_cold_quiesce(void)
{
	return controllers_retired(DMA_PRIVATE_FINALIZE);
}

struct translation_state {
	uint64_t capability;
	uint64_t extended;
	uint64_t root;
	uint32_t status;
	uint32_t version;
	struct q35_dma_pmr_state pmr;
};

static uint32_t translation_read(void *unused, uint32_t offset)
{
	(void)unused;
	return read32((void *)(uintptr_t)(VTD_BASE + offset));
}

static void translation_write(void *unused, uint32_t offset, uint32_t value)
{
	(void)unused;
	write32((void *)(uintptr_t)(VTD_BASE + offset), value);
}

static uint64_t translation_pair(uint32_t offset)
{
	return translation_read(NULL, offset) | (uint64_t)translation_read(NULL, offset + 4U) << 32;
}

static struct translation_state translation_snapshot(void)
{
	return (struct translation_state) {
		.capability = translation_pair(Q35_VTD_CAP),
		.extended = translation_pair(Q35_VTD_ECAP),
		.root = translation_pair(Q35_VTD_RTADDR),
		.status = translation_read(NULL, Q35_VTD_GSTS),
		.version = translation_read(NULL, Q35_VTD_VERSION),
		.pmr = {
			.enable = translation_read(NULL, Q35_VTD_PMEN),
			.low_base = translation_read(NULL, Q35_VTD_PLMBASE),
			.low_limit = translation_read(NULL, Q35_VTD_PLMLIMIT),
			.high_base = translation_pair(Q35_VTD_PHMBASE),
			.high_limit = translation_pair(Q35_VTD_PHMLIMIT),
		},
	};
}

static bool memory_image(struct smm_dma_owned_memory *memory,
	struct capsule_broker_buffer_reservation *reservation)
{
#if ENV_RAMSTAGE
	return platform_smm_dma_owned_memory(memory) && capsule_broker_buffers_find(reservation);
#else
	const struct smm_dma_owned_memory *stored;

	if (!smm_get_dma_owned_memory(&stored))
		return false;
	*memory = *stored;
	smm_get_capsule_broker_buffers(reservation);
	return true;
#endif
}

static bool dma_current(enum dma_wave wave)
{
	const struct q35_vtd_io io = {.read32 = translation_read, .write32 = translation_write};
	struct smm_dma_owned_memory memory, repeated_memory;
	struct capsule_broker_buffer_reservation reservation, repeated_reservation;
	struct q35_capsule_dma_geometry targets;
	struct translation_state before, after;
	uintptr_t smram_base;
	size_t smram_size;
	const uint32_t config_index = inl(PCI_IO_CONFIG_INDEX);
	bool valid = false;

	if (!controllers_retired(wave) || !smram_geometry(&smram_base, &smram_size) ||
	    !(pci_io_read_config8(HOST_BRIDGE, ESMRAMC) & T_EN) ||
	    (pci_io_read_config8(HOST_BRIDGE, SMRAMC) & (D_LCK | G_SMRAME | D_OPEN)) !=
		(D_LCK | G_SMRAME) ||
	    !memory_image(&memory, &reservation) ||
	    !span_valid(memory.table_base, memory.table_size) ||
	    !span_valid(memory.arena_base, memory.arena_size) ||
	    !span_valid(reservation.communication_base, reservation.communication_reserved_size) ||
	    !span_valid(reservation.staging_base, reservation.staging_size) ||
	    memory.table_base + memory.table_size > smram_base ||
	    memory.arena_base + memory.arena_size > smram_base ||
	    reservation.communication_base + reservation.communication_reserved_size > smram_base ||
	    reservation.staging_base + reservation.staging_size > smram_base)
		goto out;
	targets = (struct q35_capsule_dma_geometry) {
		.communication_base = reservation.communication_base,
		.communication_reserved_size = reservation.communication_reserved_size,
		.communication_size = reservation.communication_size,
		.staging_base = reservation.staging_base,
		.staging_size = reservation.staging_size,
	};
	before = translation_snapshot();
	if (before.root != memory.table_base ||
	    (before.status & (Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE |
		Q35_VTD_QUEUED_INVALIDATION_ENABLE)) !=
		(Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE) ||
	    !(before.capability & (1ULL << 10)) ||
	    (before.pmr.enable & (Q35_VTD_PMR_ENABLE | Q35_VTD_PMR_STATUS)) ||
	    q35_vtd_invalidate_drain(&io))
		goto out;
	/* Controller reset retired held mappings; BME remains clear throughout scan. */
	if (!(before.extended & 1U))
		wbinvd();
	asm volatile("mfence" ::: "memory");
	if (!q35_dma_table_image_valid((const void *)memory.table_base, memory.table_size,
		memory.arena_base, memory.arena_size, &targets) || !inventory(false) ||
	    q35_vtd_invalidate_drain(&io) ||
	    !q35_dma_table_image_valid((const void *)memory.table_base, memory.table_size,
		memory.arena_base, memory.arena_size, &targets) ||
	    !memory_image(&repeated_memory, &repeated_reservation) ||
	    memcmp(&memory, &repeated_memory, sizeof(memory)) ||
	    memcmp(&reservation, &repeated_reservation, sizeof(reservation)))
		goto out;
	after = translation_snapshot();
	if (before.capability != after.capability || before.extended != after.extended ||
	    before.root != after.root || before.status != after.status || before.version != after.version ||
	    !q35_dma_pmr_state_matches(&before.pmr, &after.pmr) || !inventory(false))
		goto out;
	if (!wave_current(wave))
		goto out;
	valid = true;
out:
	outl(config_index, PCI_IO_CONFIG_INDEX);
	return valid;
}

bool q35_dma_cold_current(void)
{
	return dma_current(DMA_PRIVATE_FINALIZE);
}

bool q35_capsule_ram_dma_current(void)
{
	return dma_current(DMA_CAPSULE_RAM);
}

#if ENV_RAMSTAGE
void smm_mainboard_pci_resource_store_init(struct smm_pci_resource_info *slots, size_t size)
{
	struct smm_pci_resource_info snapshot[PCI_FUNCTIONS];
	uintptr_t smram_base;
	size_t smram_size;
	const uint32_t config_index = inl(PCI_IO_CONFIG_INDEX);

	/* Trusted cold source only; no retrospective attestation of pre-hook RAM. */
	if (!slots || size != PCI_FUNCTIONS || !q35_dma_cold_quiesce() ||
	    !smram_geometry(&smram_base, &smram_size) || !resource_image(snapshot) ||
	    !resource_image_valid(snapshot, smram_base, smram_size) || !inventory(false))
		die("Q35 DMA: protected PCI resource capture failed\n");
	memcpy(slots, snapshot, sizeof(snapshot));
	outl(config_index, PCI_IO_CONFIG_INDEX);
}

static void before_write_tables(void *unused)
{
	(void)unused;
	if (!q35_dma_cold_current())
		die("Q35 DMA: current protection before table construction failed\n");
	printk(BIOS_INFO, "Q35 DMA: cold controller retirement and current owned deny before tables\n");
}

BOOT_STATE_INIT_ENTRY(BS_WRITE_TABLES, BS_ON_ENTRY, before_write_tables, NULL);
#endif
