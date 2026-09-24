/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_live.h"
#include "../../src/soc/intel/common/block/vtd/vtd_transition.h"

#define CAP 0x08U
#define ECAP 0x10U
#define GCMD 0x18U
#define GSTS 0x1cU
#define RTADDR 0x20U
#define CCMD 0x28U
#define PMEN 0x64U
#define IOTLB 0x108U

enum mutation {
	MUTATE_NONE,
	MUTATE_TOPOLOGY_AT_BOUNDARY,
	MUTATE_BME_AT_BOUNDARY,
};

struct pci_function {
	uint32_t vendor_device;
	uint32_t command;
	uint32_t class_revision;
};

struct pci_mock {
	struct pci_function functions[4][256];
	uint16_t ignore_write_bdf;
	enum mutation mutation;
	size_t scans;
	size_t reads;
	size_t writes;
};

struct vtd_mock {
	uint32_t registers[0x200 / sizeof(uint32_t)];
	bool committed;
};

static void add_pci(struct pci_mock *mock, uint16_t bdf, uint16_t vendor,
	uint16_t device, uint32_t class, uint16_t command)
{
	mock->functions[bdf >> 8][bdf & 0xffU] = (struct pci_function) {
		.vendor_device = (uint32_t)device << 16 | vendor,
		.command = command,
		.class_revision = class << 8,
	};
}

static uint32_t pci_read(void *context, uint8_t bus, uint8_t devfn,
	uint16_t offset)
{
	struct pci_mock *mock = context;

	if (!bus && !devfn && !offset) {
		mock->scans++;
		if (mock->scans == 3) {
			if (mock->mutation == MUTATE_TOPOLOGY_AT_BOUNDARY)
				add_pci(mock, 0x0300, 0x8086, 0xabcd, 0x060400, 0);
			else if (mock->mutation == MUTATE_BME_AT_BOUNDARY)
				mock->functions[0][0xa0].command |= 4;
		}
	}
	mock->reads++;
	if (offset == 0)
		return mock->functions[bus][devfn].vendor_device;
	if (offset == 4)
		return mock->functions[bus][devfn].command;
	return mock->functions[bus][devfn].class_revision;
}

static void pci_write(void *context, uint8_t bus, uint8_t devfn,
	uint16_t offset, uint16_t value)
{
	struct pci_mock *mock = context;
	const uint16_t bdf = (uint16_t)bus << 8 | devfn;

	assert(offset == 4);
	mock->writes++;
	if (bdf != mock->ignore_write_bdf)
		mock->functions[bus][devfn].command = value;
}

static struct pci_mock valid_pci(void)
{
	struct pci_mock mock;

	memset(&mock, 0xff, sizeof(mock));
	mock.ignore_write_bdf = UINT16_MAX;
	mock.mutation = MUTATE_NONE;
	mock.scans = 0;
	mock.reads = 0;
	mock.writes = 0;
	add_pci(&mock, 0x0200, 0x1d97, 0x0001, 0x010802, 7);
	add_pci(&mock, 0x00a0, 0x8086, 0x7e7d, 0x0c0330, 6);
	add_pci(&mock, 0x0068, 0x8086, 0x7ec0, 0x0c0330, 2);
	add_pci(&mock, 0x0100, 0x8086, 0x1234, 0x060400, 4);
	return mock;
}

static uint32_t vtd_read(void *context, uint32_t offset)
{
	return ((struct vtd_mock *)context)->registers[offset / 4U];
}

static void vtd_write(void *context, uint32_t offset, uint32_t value)
{
	struct vtd_mock *mock = context;

	mock->registers[offset / 4U] = value;
	if (offset == PMEN)
		mock->registers[PMEN / 4U] &= ~1U;
	else if (offset == GCMD && value == (1U << 30))
		mock->registers[GSTS / 4U] |= 1U << 30;
	else if (offset == GCMD && value == (1U << 31))
		mock->registers[GSTS / 4U] |= 1U << 31;
	else if (offset == CCMD + 4U && value & (1U << 31))
		mock->registers[offset / 4U] = 1U << 27;
	else if (offset == IOTLB + 4U && value & (1U << 31))
		mock->registers[offset / 4U] = 1U << 25;
}

static void vtd_commit(void *context)
{
	((struct vtd_mock *)context)->committed = true;
}

static struct vtd_mock valid_vtd(void)
{
	struct vtd_mock mock = { 0 };

	mock.registers[0] = 0x10;
	mock.registers[CAP / 4U] = 1U << 10;
	mock.registers[ECAP / 4U] = 0x10U << 8 | 1U;
	mock.registers[PMEN / 4U] = 1U << 31 | 1U;
	return mock;
}

static bool memory_is(const uint8_t *memory, size_t size, uint8_t value)
{
	for (size_t index = 0; index < size; index++)
		if (memory[index] != value)
			return false;
	return true;
}

int main(int argc, char **argv)
{
	const struct starbook_mtl_dma_live_identity expected[] = {
		{ .bdf = 0x0200, .vendor = 0x1d97, .device = 1,
		  .class = 0x010802 },
		{ .bdf = 0x00a0, .vendor = 0x8086, .device = 0x7e7d,
		  .class = 0x0c0330 },
		{ .bdf = 0x0068, .vendor = 0x8086, .device = 0x7ec0,
		  .class = 0x0c0330 },
	};
	const size_t memory_size = 2U * 1024U * 1024U;
	struct pci_mock pci = valid_pci();
	const struct starbook_mtl_dma_live_pci_io pci_io = {
		.context = &pci,
		.read32 = pci_read,
		.write16 = pci_write,
	};
	struct vtd_mock vtd = valid_vtd();
	const struct vtd_transition_io transition = {
		.context = &vtd,
		.read32 = vtd_read,
		.write32 = vtd_write,
		.commit_tables = vtd_commit,
	};
	struct dma_handoff_requester handoff[STARBOOK_MTL_DMA_LIVE_REQUESTERS];
	const struct starbook_mtl_dma_live_layout *layout;
	void *memory;
	void *mirror;
	void *establish_mirror;
	uint64_t establish_mirror_physical = 0x400000;
	size_t mirror_size;
	size_t establish_size = memory_size;
	int result;

	assert(argc == 2);
	assert(!posix_memalign(&memory, 4096, memory_size));
	assert(!starbook_mtl_dma_live_table_mirror_size(0x800000, memory_size,
		&mirror_size));
	assert(!posix_memalign(&mirror, 4096, mirror_size));
	memset(memory, 0xa5, memory_size);
	memset(mirror, 0xa5, mirror_size);
	establish_mirror = mirror;
	if (!strcmp(argv[1], "snapshot-failure"))
		pci.functions[2][0].vendor_device ^= 1U << 16;
	else if (!strcmp(argv[1], "clear-failure"))
		pci.ignore_write_bdf = expected[0].bdf;
	else if (!strcmp(argv[1], "topology-boundary"))
		pci.mutation = MUTATE_TOPOLOGY_AT_BOUNDARY;
	else if (!strcmp(argv[1], "bme-boundary"))
		pci.mutation = MUTATE_BME_AT_BOUNDARY;
	else if (!strcmp(argv[1], "noncoherent"))
		vtd.registers[ECAP / 4U] &= ~1U;
	else if (!strcmp(argv[1], "capacity")) {
		memset(&pci, 0xff, sizeof(pci));
		pci.ignore_write_bdf = UINT16_MAX;
		pci.mutation = MUTATE_NONE;
		pci.scans = pci.reads = pci.writes = 0;
		for (uint16_t bus = 0; bus < 2; bus++)
			for (uint16_t devfn = 0; devfn < 256; devfn++)
				add_pci(&pci, bus << 8 | devfn, 0x8086,
					0x1000U + devfn, 0x060400, 0);
		add_pci(&pci, 0x0200, 0x1d97, 1, 0x010802, 0);
		add_pci(&pci, 0x00a0, 0x8086, 0x7e7d, 0x0c0330, 0);
		add_pci(&pci, 0x0068, 0x8086, 0x7ec0, 0x0c0330, 0);
	} else if (!strcmp(argv[1], "unaligned-size")) {
		establish_size--;
	} else if (!strcmp(argv[1], "mirror-virtual-misaligned")) {
		establish_mirror = (uint8_t *)mirror + 1U;
	} else if (!strcmp(argv[1], "mirror-physical-misaligned")) {
		establish_mirror_physical++;
	} else if (!strcmp(argv[1], "mirror-physical-alias")) {
		/* Distinct virtual storage must not alias the live physical range. */
		establish_mirror_physical = 0x800000;
	} else if (!strcmp(argv[1], "mirror-virtual-overlap")) {
		establish_mirror = memory;
	} else if (strcmp(argv[1], "success") &&
		   strcmp(argv[1], "active-selected-bme") &&
		   strcmp(argv[1], "active-unlisted-bme") &&
		   strcmp(argv[1], "active-topology") &&
		   strcmp(argv[1], "active-combined-poison")) {
		assert(false);
	}

	result = starbook_mtl_dma_live_establish(&pci_io, 4, expected, memory,
		0x800000, establish_size, establish_mirror,
		establish_mirror_physical, mirror_size, &transition);
	if (strcmp(argv[1], "success") &&
	    strcmp(argv[1], "active-selected-bme") &&
	    strcmp(argv[1], "active-unlisted-bme") &&
	    strcmp(argv[1], "active-topology") &&
	    strcmp(argv[1], "active-combined-poison")) {
		void *second;
		const size_t reads = pci.reads;
		const size_t writes = pci.writes;

		assert(result);
		assert(memory_is(memory, memory_size, 0xa5));
		assert(!starbook_mtl_dma_live_layout());
		assert(!vtd.committed);
		assert(vtd.registers[PMEN / 4U] & 1U);
		if (!strcmp(argv[1], "snapshot-failure") ||
		    !strcmp(argv[1], "noncoherent") ||
		    !strcmp(argv[1], "capacity") ||
		    !strcmp(argv[1], "unaligned-size"))
			assert(!pci.writes);
		else
			assert(pci.writes);
		if (!strcmp(argv[1], "bme-boundary"))
			assert(!(pci.functions[0][0xa0].command & 4));
		assert(!posix_memalign(&second, 4096, memory_size));
		memset(second, 0xa5, memory_size);
		assert(starbook_mtl_dma_live_establish(&pci_io, 4, expected, second,
			0xa00000, memory_size, mirror, 0x400000, mirror_size,
			&transition));
		assert(memory_is(second, memory_size, 0xa5));
		assert(pci.reads == reads && pci.writes == writes);
		free(second);
		free(memory);
		free(mirror);
		return 0;
	}

	assert(!result && vtd.committed);
	assert(!memory_is(memory, memory_size, 0xa5));
	assert(starbook_mtl_dma_live_verify_active(&pci_io, 4));
	layout = starbook_mtl_dma_live_layout();
	assert(layout && layout->table_used_pages <= layout->table_capacity_pages);
	assert(layout->table_mirror == mirror &&
		layout->table_mirror_physical == 0x400000);
	assert(layout->arena_base[STARBOOK_MTL_DMA_LIVE_REQUESTERS - 1U] +
		(uint64_t)layout->arena_pages[
			STARBOOK_MTL_DMA_LIVE_REQUESTERS - 1U] * 4096U ==
		0x800000U + memory_size);
	assert(!memcmp(layout->table_memory, layout->table_mirror,
		layout->table_capacity_pages * 4096U));
	assert(starbook_mtl_dma_live_tables_match(layout));
	((uint8_t *)layout->table_memory)[0] ^= 1U;
	assert(!starbook_mtl_dma_live_tables_match(layout));
	((uint8_t *)layout->table_memory)[0] ^= 1U;
	((uint8_t *)layout->table_mirror)[
		layout->table_capacity_pages * 4096U - 1U] ^= 1U;
	assert(!starbook_mtl_dma_live_tables_match(layout));
	((uint8_t *)layout->table_mirror)[
		layout->table_capacity_pages * 4096U - 1U] ^= 1U;
	assert(starbook_mtl_dma_live_handoff_requesters(handoff));
	{
		const uint16_t present[] = { expected[0].bdf, expected[1].bdf };
		const uint16_t missing[] = { expected[0].bdf, 0x00ff };

		assert(starbook_mtl_dma_live_devices_are_verified(&pci_io, 4,
			present, 2));
		assert(!starbook_mtl_dma_live_devices_are_verified(&pci_io, 4,
			missing, 2));
	}
	for (size_t index = 0; index < STARBOOK_MTL_DMA_LIVE_REQUESTERS; index++)
		assert(handoff[index].bdf == expected[index].bdf);
	{
		void *second;
		const size_t reads = pci.reads;
		const size_t writes = pci.writes;

		assert(!posix_memalign(&second, 4096, memory_size));
		memset(second, 0xa5, memory_size);
		assert(starbook_mtl_dma_live_establish(&pci_io, 4, expected, second,
			0xa00000, memory_size, mirror, 0x400000, mirror_size,
			&transition));
		assert(memory_is(second, memory_size, 0xa5));
		assert(pci.reads == reads && pci.writes == writes);
		free(second);
	}
	if (!strcmp(argv[1], "success")) {
		assert(starbook_mtl_dma_live_verify_active(&pci_io, 4));
	} else {
		void *second;
		size_t reads;
		size_t writes;

		if (!strcmp(argv[1], "active-selected-bme"))
			pci.functions[0][0xa0].command |= 4;
		else if (!strcmp(argv[1], "active-unlisted-bme"))
			pci.functions[1][0].command |= 4;
		else if (!strcmp(argv[1], "active-topology"))
			add_pci(&pci, 0x0300, 0x8086, 0xabcd, 0x060400, 4);
		else {
			((uint8_t *)layout->table_memory)[0] ^= 1U;
			pci.functions[1][0].command |= 4;
			starbook_mtl_dma_live_poison(&pci_io, 4);
		}
		if (strcmp(argv[1], "active-combined-poison"))
			assert(!starbook_mtl_dma_live_verify_active(&pci_io, 4));
		for (uint16_t bus = 0; bus < 4; bus++)
			for (uint16_t devfn = 0; devfn <= UINT8_MAX; devfn++)
				if ((uint16_t)pci.functions[bus][devfn].vendor_device !=
				    UINT16_MAX)
					assert(!(pci.functions[bus][devfn].command & 4));
		assert(!starbook_mtl_dma_live_layout());
		assert(!starbook_mtl_dma_live_handoff_requesters(handoff));
		reads = pci.reads;
		writes = pci.writes;
		assert(!posix_memalign(&second, 4096, memory_size));
		memset(second, 0xa5, memory_size);
		assert(starbook_mtl_dma_live_establish(&pci_io, 4, expected, second,
			0xa00000, memory_size, mirror, 0x400000, mirror_size,
			&transition));
		assert(memory_is(second, memory_size, 0xa5));
		assert(pci.reads == reads && pci.writes == writes);
		free(second);
	}
	free(memory);
	free(mirror);
	return 0;
}
