/* SPDX-License-Identifier: GPL-2.0-only */

/* HOST platform model. This exercises the real join, not hardware authority. */
#include "host-platform.h"
#include "q35_dma_cold.h"
#include "vtd_registers.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ECAM 0xb0000000U
#define VTD 0xfed90000U
#define PAGE 4096U
#define NVME 0xe0000000U
#define XHCI 0xe0010000U
#define AHCI 0xe0020000U
#define EDU 0xe0100000U

static uint8_t pci[8][4096];
static uint8_t extra_pci[2][4096];
static uint16_t extra_bdfs[2];
static bool extra_present[2];
static unsigned int root_identity_reads[256], other_identity_reads;
static uint32_t translation[PAGE / 4U];
static const uint16_t bdfs[8] = {0, 8, 0x18, 0x20, 0x28, 0xf8, 0xfa, 0xfb};
static const uint32_t identities[8] = {
	0x29c08086, 0x11111234, 0x00101b36, 0x000d1b36,
	0x11e81234, 0x29188086, 0x29228086, 0x29308086,
};
static struct smm_pci_resource_info resources[8];
static uint8_t tables[10U * PAGE] __attribute__((aligned(PAGE)));
static uint8_t arena[161U * PAGE] __attribute__((aligned(PAGE)));
static uint8_t communication[PAGE] __attribute__((aligned(PAGE)));
static uint8_t staging[2U * PAGE] __attribute__((aligned(PAGE)));
static struct smm_dma_owned_memory memory;
static struct capsule_broker_buffer_reservation buffers;
static struct smm_invocation_runtime_view view;
static uint32_t config_index, saved_index;
static unsigned int nvme_resets, xhci_resets, ahci_resets, drains, writebacks;
static uint32_t nvme_configuration, nvme_status, xhci_command, xhci_status;
static uint32_t ahci_command, ahci_active;
static bool phase_current, foreign_function, mutate_memory, fail_ahci, fail_drain;
static bool protected_resources;
static bool ram_held, ram_window, revoke_ram_after_drain;
static bool stuck_master, late_master, late_function, change_ecam;

/* HOST-only access to the actual static scanner, appended by the test script. */
bool host_inventory(bool clear_master);

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		abort();
}

static void check(bool condition, const char *expression)
{
	if (condition)
		return;
	fprintf(stderr, "DMA_COLD_ASSERT: %s\n", expression);
	assert(condition);
}

#define CHECK(expression) check(!!(expression), #expression)

static int pci_slot(uintptr_t address, uint32_t *offset)
{
	if (address < ECAM || address >= (uintptr_t)ECAM + 0x10000000U)
		return -1;
	*offset = (uint32_t)((address - ECAM) & 4095U);
	for (size_t slot = 0; slot < 8; slot++)
		if ((address - ECAM) >> 12 == bdfs[slot])
			return (int)slot;
	return -2;
}

uint32_t read32(const void *pointer)
{
	const uintptr_t address = (uintptr_t)pointer;
	uint32_t offset = 0;
	const int slot = pci_slot(address, &offset);
	uint32_t value;

	if (slot != -1 && !offset) {
		const uint32_t bdf = (uint32_t)((address - ECAM) >> 12);

		if (bdf <= UINT8_MAX) {
			root_identity_reads[bdf]++;
			if (change_ecam && bdf == UINT8_MAX)
				pci[0][0x60] ^= 1U;
		} else {
			other_identity_reads++;
		}
	}
	if (slot >= 0) {
		if (offset > sizeof(pci[slot]) - sizeof(value))
			return UINT32_MAX;
		memcpy(&value, &pci[slot][offset], sizeof(value));
		return value;
	}
	if (slot == -2) {
		for (size_t i = 0; i < 2; i++) {
			if (!extra_present[i] || (address - ECAM) >> 12 != extra_bdfs[i])
				continue;
			CHECK(offset <= sizeof(extra_pci[i]) - sizeof(value));
			memcpy(&value, &extra_pci[i][offset], sizeof(value));
			return value;
		}
		return foreign_function && address == (uintptr_t)ECAM + (0x30U << 12) ?
			0x100e8086U : UINT32_MAX;
	}
	if (address >= VTD && address < (uintptr_t)VTD + PAGE)
		return translation[(address - VTD) / 4U];
	if (address >= NVME && address < (uintptr_t)NVME + PAGE) {
		switch (address - NVME) {
		case 0: return 0x10001U;
		case 4: return 0;
		case 8: return 0x10400U;
		case 0x14: return nvme_configuration;
		case 0x1c: return nvme_status;
		default: return 0;
		}
	}
	if (address >= XHCI && address < (uintptr_t)XHCI + PAGE) {
		switch (address - XHCI) {
		case 0: return 0x1000040U;
		case 0x40: return xhci_command;
		case 0x44: return xhci_status;
		default: return 0;
		}
	}
	if (address >= AHCI && address < (uintptr_t)AHCI + PAGE) {
		switch (address - AHCI) {
		case 0: return 5U;
		case 4: return ahci_command;
		case 0x0c: return 0x3fU;
		case 0x10: return 0x10300U;
		case 0x134: return ahci_active;
		default: return 0;
		}
	}
	if (address == EDU + 0x98U || address == EDU + 0x9cU)
		return 0;
	CHECK(false);
	return UINT32_MAX;
}

uint16_t read16(const void *pointer)
{
	const uintptr_t address = (uintptr_t)pointer;
	return (uint16_t)(read32((const void *)(address & ~(uintptr_t)3U)) >> ((address & 2U) * 8U));
}

uint8_t read8(const void *pointer)
{
	const uintptr_t address = (uintptr_t)pointer;
	return (uint8_t)(read32((const void *)(address & ~(uintptr_t)3U)) >> ((address & 3U) * 8U));
}

uint64_t read64(const void *pointer)
{
	return read32(pointer) | (uint64_t)read32((const uint8_t *)pointer + 4U) << 32;
}

void write32(void *pointer, uint32_t value)
{
	const uintptr_t address = (uintptr_t)pointer;
	uint32_t offset = 0;
	const int slot = pci_slot(address, &offset);

	if (slot >= 0) {
		CHECK(offset <= sizeof(pci[slot]) - sizeof(value));
		if (stuck_master && slot == 2 && offset == PCI_COMMAND)
			value |= PCI_COMMAND_MASTER;
		memcpy(&pci[slot][offset], &value, sizeof(value));
		return;
	}
	if (slot == -2) {
		for (size_t i = 0; i < 2; i++) {
			if (!extra_present[i] || (address - ECAM) >> 12 != extra_bdfs[i])
				continue;
			CHECK(offset <= sizeof(extra_pci[i]) - sizeof(value));
			memcpy(&extra_pci[i][offset], &value, sizeof(value));
		}
		return;
	}
	if (address >= VTD && address < (uintptr_t)VTD + PAGE) {
		if ((value & (1U << 31)) && address == VTD + Q35_VTD_CCMD + 4U)
			value = 1U << 27;
		if ((value & (1U << 31)) && address == VTD + 0x108U + 4U) {
			if ((value & ((1U << 17) | (1U << 16))) == ((1U << 17) | (1U << 16))) {
				drains++;
				if (late_function && drains == 2U)
					extra_present[0] = true;
				if (late_master && drains == 2U)
					pci[2][PCI_COMMAND] |= PCI_COMMAND_MASTER;
				if (mutate_memory && drains == 2U)
					memory.arena_base += PAGE;
				if (revoke_ram_after_drain && drains == 2U)
					ram_window = false;
			}
			value = fail_drain ? 1U << 31 : 1U << 25;
		}
		translation[(address - VTD) / 4U] = value;
		return;
	}
	if (address == NVME + 0x14U) {
		nvme_configuration = value;
		if (!(value & 1U)) {
			nvme_resets++;
			nvme_status = 0;
		}
		return;
	}
	if (address == XHCI + 0x40U) {
		xhci_command = value;
		if (!(value & 1U))
			xhci_status = 1U;
		if (value & 2U) {
			xhci_resets++;
			xhci_command = 0;
			xhci_status = 1U;
		}
		return;
	}
	if (address == AHCI + 4U) {
		ahci_command = value & ~1U;
		ahci_resets++;
		if (!fail_ahci)
			ahci_active = 0;
		return;
	}
	CHECK(false);
}

void write16(void *pointer, uint16_t value)
{
	const uintptr_t address = (uintptr_t)pointer;
	const uint32_t shift = (uint32_t)(address & 2U) * 8U;
	uint32_t word = read32((void *)(address & ~(uintptr_t)3U));

	word = (word & ~(0xffffU << shift)) | (uint32_t)value << shift;
	write32((void *)(address & ~(uintptr_t)3U), word);
}

uint32_t inl(uint16_t port)
{
	CHECK(port == PCI_IO_CONFIG_INDEX);
	return config_index;
}

void outl(uint32_t value, uint16_t port)
{
	CHECK(port == PCI_IO_CONFIG_INDEX);
	config_index = value;
}

uint32_t pci_io_read_config32(pci_devfn_t device, uint16_t offset)
{
	config_index = device | offset;
	return read32((void *)((uintptr_t)ECAM + device + offset));
}

uint8_t pci_io_read_config8(pci_devfn_t device, uint16_t offset)
{
	config_index = device | offset;
	return read8((void *)((uintptr_t)ECAM + device + offset));
}

void udelay(unsigned int microseconds)
{
	(void)microseconds;
}

void wbinvd(void)
{
	writebacks++;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = 0x10000000U;
	*size = 0x800000U;
}

bool platform_payload_mm_authvar_service_finalize_admitted(void)
{
	return phase_current;
}

bool q35_capsule_ram_transaction_current(void)
{
	return ram_held && ram_window;
}

const volatile struct smm_pci_resource_info *smm_get_pci_resource_store(void)
{
	return resources;
}

enum cb_err smm_invocation_runtime_view_get(const struct smm_invocation_runtime_view **output)
{
	*output = &view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_cpu_count(const struct smm_invocation_runtime_view *input,
	uint32_t *cpus)
{
	CHECK(input == &view);
	*cpus = 1;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_geometry_is_contained(const struct smm_invocation_runtime_view *input,
	uintptr_t base, size_t size)
{
	CHECK(input == &view && base == 0x10000000U && size == 0x800000U);
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(const struct smm_invocation_runtime_view *input,
	const void *base, size_t size)
{
	CHECK(input == &view && base == resources && size == sizeof(resources));
	return protected_resources ? CB_SUCCESS : CB_ERR;
}

bool smm_get_dma_owned_memory(const struct smm_dma_owned_memory **output)
{
	*output = &memory;
	return true;
}

void smm_get_capsule_broker_buffers(struct capsule_broker_buffer_reservation *output)
{
	*output = buffers;
}

static void set32(size_t slot, size_t offset, uint32_t value)
{
	memcpy(&pci[slot][offset], &value, sizeof(value));
}

static void aperture(size_t slot, uint32_t bar, uint32_t base, uint32_t size, bool pci64)
{
	set32(slot, bar, base | (pci64 ? 4U : 0U));
	resources[slot].resources[0] = (struct resource) {
		.index = bar, .base = base, .size = size,
		.flags = IORESOURCE_MEM | IORESOURCE_ASSIGNED | IORESOURCE_STORED |
			(pci64 ? IORESOURCE_PCI64 : 0U),
	};
}

/* This is the same fixed image validated by the independent population test. */
static void table_image(void)
{
	uint64_t *root = (void *)tables;
	uint64_t *context = (void *)(tables + PAGE);
	const uint16_t requesters[] = {0x18, 0x20};
	const uint64_t iovas[] = {0x80000000ULL, 0x90000000ULL};
	const size_t page_counts[] = {32, 128};

	memset(tables, 0, sizeof(tables));
	root[0] = (uintptr_t)context | 1U;
	for (size_t i = 0; i < 2; i++) {
		uint64_t *pml4 = (void *)(tables + (2U + i * 4U) * PAGE);
		uint64_t *pdpt = (void *)((uint8_t *)pml4 + PAGE);
		uint64_t *pd = (void *)((uint8_t *)pdpt + PAGE);
		uint64_t *pt = (void *)((uint8_t *)pd + PAGE);
		const uintptr_t physical = (uintptr_t)arena + (i ? 32U * PAGE : 0U);

		context[requesters[i] * 2U] = (uintptr_t)pml4 | 1U;
		context[requesters[i] * 2U + 1U] = 2U | ((i + 1U) << 8);
		pml4[(iovas[i] >> 39) & 511U] = (uintptr_t)pdpt | 3U;
		pdpt[(iovas[i] >> 30) & 511U] = (uintptr_t)pd | 3U;
		pd[(iovas[i] >> 21) & 511U] = (uintptr_t)pt | 3U;
		for (size_t page = 0; page < page_counts[i]; page++)
			pt[((iovas[i] >> 12) + page) & 511U] = (physical + page * PAGE) | 3U;
	}
}

static void reset_fixture(void)
{
	memset(pci, 0, sizeof(pci));
	memset(extra_pci, 0, sizeof(extra_pci));
	memset(extra_present, 0, sizeof(extra_present));
	memset(root_identity_reads, 0, sizeof(root_identity_reads));
	other_identity_reads = 0;
	memset(resources, 0, sizeof(resources));
	memset(translation, 0, sizeof(translation));
	for (size_t slot = 0; slot < 8; slot++) {
		set32(slot, 0, identities[slot]);
		set32(slot, 4, PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
		resources[slot].pci_addr = (uint32_t)bdfs[slot] << 12;
		resources[slot].vendor_id = (uint16_t)identities[slot];
		resources[slot].device_id = (uint16_t)(identities[slot] >> 16);
	}
	set32(0, 0x60, ECAM | 1U);
	set32(0, 0x9c, 0x00051b00U);
	aperture(1, 0x10, 0xd0000000U, 0x1000000U, false);
	aperture(2, 0x10, NVME, PAGE, true);
	aperture(3, 0x10, XHCI, PAGE, true);
	aperture(4, 0x10, EDU, 0x100000U, false);
	aperture(6, 0x24, AHCI, PAGE, false);
	memory = (struct smm_dma_owned_memory) {
		.table_base = (uintptr_t)tables, .table_size = sizeof(tables),
		.arena_base = (uintptr_t)arena, .arena_size = sizeof(arena),
	};
	buffers = (struct capsule_broker_buffer_reservation) {
		.communication_base = (uintptr_t)communication,
		.communication_reserved_size = sizeof(communication), .communication_size = 96,
		.staging_base = (uintptr_t)staging, .staging_size = sizeof(staging),
	};
	table_image();
	translation[Q35_VTD_VERSION / 4U] = 0x10U;
	translation[Q35_VTD_CAP / 4U] = 1U << 10;
	translation[Q35_VTD_CAP / 4U + 1U] = (1U << 23) | (1U << 22);
	translation[Q35_VTD_ECAP / 4U] = 0x1000U;
	translation[Q35_VTD_GSTS / 4U] = Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE;
	translation[Q35_VTD_RTADDR / 4U] = (uint32_t)(uintptr_t)tables;
	nvme_configuration = nvme_status = xhci_command = ahci_active = 1U;
	xhci_status = ahci_command = 0;
	nvme_resets = xhci_resets = ahci_resets = drains = writebacks = 0;
	phase_current = true;
	protected_resources = true;
	ram_held = ram_window = true;
	revoke_ram_after_drain = false;
	foreign_function = mutate_memory = fail_ahci = fail_drain = false;
	stuck_master = late_master = late_function = change_ecam = false;
	saved_index = config_index = 0x31415926U;
}

static void extra_function(size_t index, uint16_t bdf, uint32_t identity, uint8_t header)
{
	CHECK(index < 2);
	extra_bdfs[index] = bdf;
	extra_present[index] = true;
	memcpy(extra_pci[index], &identity, sizeof(identity));
	extra_pci[index][PCI_HEADER_TYPE] = header;
	extra_pci[index][PCI_COMMAND] = PCI_COMMAND_MASTER;
}

static void inventory_cases(void)
{
	reset_fixture();
	CHECK(host_inventory(true));
	CHECK(!other_identity_reads);
	for (size_t devfn = 0; devfn <= UINT8_MAX; devfn++)
		CHECK(root_identity_reads[devfn]);
	CHECK(host_inventory(false));
	CHECK(!other_identity_reads);
	/* Never reuse a prior accepted image: every other root devfn is hostile. */
	for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++) {
		bool expected = false;

		for (size_t i = 0; i < 8; i++)
			expected |= bdfs[i] == devfn;
		if (expected)
			continue;
		reset_fixture();
		extra_function(0, devfn, 0x00101b36U, PCI_HEADER_TYPE_NORMAL);
		CHECK(!host_inventory(true));
		CHECK(other_identity_reads == 255U * 256U);
		CHECK(!(extra_pci[0][PCI_COMMAND] & PCI_COMMAND_MASTER));
	}
	/* Captured actual QEMU PXB-PCIE/CXL and ordinary bridge root identities. */
	const uint32_t extra_identities[] = {0x000b1b36U, 0x00011b36U};
	const uint8_t headers[] = {PCI_HEADER_TYPE_NORMAL, PCI_HEADER_TYPE_BRIDGE};
	for (size_t i = 0; i < 2; i++) {
		reset_fixture();
		extra_function(0, 0x30, extra_identities[i], headers[i]);
		extra_function(1, 0x3408, 0x11e81234U, PCI_HEADER_TYPE_NORMAL);
		CHECK(!host_inventory(true));
		CHECK(other_identity_reads == 255U * 256U);
		CHECK(!(extra_pci[1][PCI_COMMAND] & PCI_COMMAND_MASTER));
	}
}

int main(int argc, char **argv)
{
	if (argc == 2) {
		reset_fixture();
		if (!strncmp(argv[1], "inventory-", 10)) {
			if (!strcmp(argv[1], "inventory-fast")) {
				CHECK(host_inventory(true));
				CHECK(!other_identity_reads);
				return 0;
			}
			if (!strcmp(argv[1], "inventory-bridge"))
				pci[2][PCI_HEADER_TYPE] = PCI_HEADER_TYPE_BRIDGE;
			else if (!strcmp(argv[1], "inventory-count"))
				set32(7, PCI_VENDOR_ID, UINT32_MAX);
			else if (!strcmp(argv[1], "inventory-pxb"))
				extra_function(0, 0xff, 0x000b1b36U, PCI_HEADER_TYPE_NORMAL);
			else if (!strcmp(argv[1], "inventory-bme"))
				stuck_master = true;
			else if (!strcmp(argv[1], "inventory-ecam"))
				change_ecam = true;
			else if (!strcmp(argv[1], "inventory-cleanup")) {
				extra_function(0, 0x30, 0x000b1b36U, PCI_HEADER_TYPE_NORMAL);
				extra_function(1, 0x3408, 0x11e81234U, PCI_HEADER_TYPE_NORMAL);
			} else {
				CHECK(false);
			}
			CHECK(!host_inventory(true));
			if (!strcmp(argv[1], "inventory-cleanup"))
				CHECK(!(extra_pci[1][PCI_COMMAND] & PCI_COMMAND_MASTER));
			printf("PASS HOST refusal case %s\n", argv[1]);
			return 0;
		}
		if (!strcmp(argv[1], "ats"))
			set32(2, 0x100, 0x1000fU);
		else if (!strcmp(argv[1], "root"))
			translation[Q35_VTD_RTADDR / 4U] += PAGE;
		else if (!strcmp(argv[1], "pmr"))
			translation[Q35_VTD_PMEN / 4U] = Q35_VTD_PMR_ENABLE;
		else if (!strcmp(argv[1], "memory"))
			mutate_memory = true;
		else if (!strcmp(argv[1], "protected"))
			protected_resources = false;
		else if (!strcmp(argv[1], "phase"))
			phase_current = false;
		else if (!strcmp(argv[1], "ram-held"))
			ram_held = false;
		else if (!strcmp(argv[1], "ram-window"))
			ram_window = false;
		else if (!strcmp(argv[1], "ram-final"))
			revoke_ram_after_drain = true;
		else
			CHECK(false);
		const bool accepted = !strncmp(argv[1], "ram-", 4) ?
			q35_capsule_ram_dma_current() : q35_dma_cold_current();

		CHECK(!accepted);
		if (!strcmp(argv[1], "phase") || !strcmp(argv[1], "protected") ||
		    !strcmp(argv[1], "ram-held") || !strcmp(argv[1], "ram-window"))
			CHECK(!nvme_resets && !xhci_resets && !ahci_resets);
		printf("PASS HOST refusal case %s\n", argv[1]);
		return 0;
	}
	CHECK(argc == 1);
	inventory_cases();
	reset_fixture();
	phase_current = false;
	CHECK(q35_capsule_ram_dma_current());
	CHECK(nvme_resets == 1U && xhci_resets == 1U && ahci_resets == 1U && drains == 2U);
	reset_fixture();
	ram_held = false;
	CHECK(!q35_capsule_ram_dma_current());
	CHECK(!nvme_resets && !xhci_resets && !ahci_resets && !drains);
	reset_fixture();
	revoke_ram_after_drain = true;
	CHECK(!q35_capsule_ram_dma_current());
	CHECK(nvme_resets == 1U && xhci_resets == 1U && ahci_resets == 1U && drains == 2U);
	reset_fixture();
	ram_window = false;
	CHECK(!q35_capsule_ram_dma_current());
	CHECK(!nvme_resets && !xhci_resets && !ahci_resets && !drains);
	reset_fixture();
	CHECK(q35_dma_cold_current());
	CHECK(nvme_resets == 1U && xhci_resets == 1U && ahci_resets == 1U);
	CHECK(drains == 2U && writebacks == 1U && config_index == saved_index);
	CHECK(!other_identity_reads);
	for (size_t devfn = 0; devfn <= UINT8_MAX; devfn++)
		CHECK(root_identity_reads[devfn] >= 4U);
	reset_fixture();
	extra_function(0, 0xff, 0x000b1b36U, PCI_HEADER_TYPE_NORMAL);
	extra_present[0] = false;
	late_function = true;
	CHECK(!q35_dma_cold_current());
	CHECK(drains == 2U && root_identity_reads[255] >= 4U);
	reset_fixture();
	late_master = true;
	CHECK(!q35_dma_cold_current());
	CHECK(drains == 2U);
	reset_fixture();
	phase_current = false;
	CHECK(!q35_dma_cold_current());
	CHECK(!nvme_resets && !xhci_resets && !ahci_resets && config_index == saved_index);
	reset_fixture();
	foreign_function = true;
	CHECK(!q35_dma_cold_current());
	CHECK(!nvme_resets && !(read16((void *)(ECAM + 4U)) & PCI_COMMAND_MASTER));
	reset_fixture();
	set32(2, 0x100, 0x1000fU);
	CHECK(!q35_dma_cold_current());
	CHECK(!nvme_resets && !xhci_resets && !ahci_resets);
	reset_fixture();
	set32(2, 0x100, 0x10010001U);
	CHECK(!q35_dma_cold_current());
	reset_fixture();
	set32(2, 0x14, 1U);
	CHECK(!q35_dma_cold_current());
	CHECK(!nvme_resets && !xhci_resets && !ahci_resets);
	reset_fixture();
	resources[2].resources[0].base = (uintptr_t)communication;
	CHECK(!q35_dma_cold_current());
	CHECK(!nvme_resets && !xhci_resets && !ahci_resets);
	reset_fixture();
	translation[Q35_VTD_RTADDR / 4U] += PAGE;
	CHECK(!q35_dma_cold_current());
	reset_fixture();
	translation[Q35_VTD_PMEN / 4U] = Q35_VTD_PMR_ENABLE;
	CHECK(!q35_dma_cold_current());
	reset_fixture();
	translation[Q35_VTD_GSTS / 4U] |= Q35_VTD_QUEUED_INVALIDATION_ENABLE;
	CHECK(!q35_dma_cold_current());
	reset_fixture();
	((uint64_t *)tables)[4U * 512U + 3U] = (uintptr_t)communication | 3U;
	CHECK(!q35_dma_cold_current());
	reset_fixture();
	fail_ahci = true;
	CHECK(!q35_dma_cold_current());
	reset_fixture();
	fail_drain = true;
	CHECK(!q35_dma_cold_current());
	reset_fixture();
	mutate_memory = true;
	CHECK(!q35_dma_cold_current());
	puts("PASS HOST cold current join only (modeled platform, not authority)");
	return 0;
}
