/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_diagnostic.h"

struct mock {
	uint32_t engine[0x80 / sizeof(uint32_t)];
	uint16_t vendor[4][256];
	uint16_t command[4][256];
	uint32_t engine_reads;
	uint32_t pci_reads;
};

static uint32_t read_engine(void *context, uint32_t offset)
{
	struct mock *mock = context;

	mock->engine_reads++;
	return mock->engine[offset / sizeof(uint32_t)];
}

static uint16_t read_pci(void *context, uint8_t bus, uint8_t devfn,
	uint16_t offset)
{
	struct mock *mock = context;

	mock->pci_reads++;
	return offset ? mock->command[bus][devfn] : mock->vendor[bus][devfn];
}

int main(void)
{
	struct mock mock;
	const struct starbook_mtl_dma_diagnostic_io io = {
		.context = &mock,
		.read_engine32 = read_engine,
		.read_pci16 = read_pci,
	};
	const uint16_t requesters[] = { 0x0200, 0x00a0, 0x0068 };
	struct starbook_mtl_dma_facts facts;

	memset(&mock, 0, sizeof(mock));
	memset(mock.vendor, 0xff, sizeof(mock.vendor));
	mock.engine[0] = 0x10;
	mock.engine[2] = 0x12345678;
	mock.engine[3] = 0x9abcdef0;
	mock.engine[4] = 0x11111111;
	mock.engine[5] = 0x22222222;
	mock.engine[7] = 0xc0000000;
	mock.engine[8] = 0x34567000;
	mock.engine[9] = 1;
	mock.engine[0x64 / 4] = 0x80000001;
	mock.engine[0x68 / 4] = 0;
	mock.engine[0x6c / 4] = 0x7fffffff;
	mock.engine[0x70 / 4] = 0;
	mock.engine[0x74 / 4] = 1;
	mock.engine[0x78 / 4] = 0xffffffff;
	mock.engine[0x7c / 4] = 1;
	mock.vendor[2][0] = 0x1d97;
	mock.vendor[0][0xa0] = 0x8086;
	mock.vendor[0][0x68] = 0x8086;
	mock.vendor[1][0] = 0x8086;
	mock.command[0][0xa0] = 4;
	mock.command[1][0] = 4;

	assert(starbook_mtl_dma_collect_facts(&io, 4, requesters,
		0x800000, 0x200000, &facts));
	assert(facts.version == 0x10);
	assert(facts.capability == 0x9abcdef012345678ULL);
	assert(facts.extended_capability == 0x2222222211111111ULL);
	assert(facts.status == 0xc0000000);
	assert(facts.root == 0x134567000ULL);
	assert(facts.protected_memory_enable == 0x80000001);
	assert(facts.protected_high_base == 0x100000000ULL);
	assert(facts.protected_high_limit == 0x1ffffffffULL);
	assert(facts.dma_buffer_base == 0x800000);
	assert(facts.dma_buffer_size == 0x200000);
	assert(facts.pci_function_count == 4);
	assert(facts.bus_master_count == 2);
	assert(facts.bus_master_bdf[0] == 0x00a0);
	assert(facts.bus_master_bdf[1] == 0x0100);
	assert(facts.requester_present[0] && facts.requester_present[1] &&
	       facts.requester_present[2]);
	assert(!facts.requester_bus_master[0]);
	assert(facts.requester_bus_master[1]);
	assert(!facts.requester_bus_master[2]);
	assert(mock.engine_reads == 15);
	assert(mock.pci_reads == 4U * 256U + 4U);

	assert(!starbook_mtl_dma_collect_facts(&io, 2, requesters,
		0x800000, 0x200000, &facts));
	{
		const uint16_t duplicate[] = { 0x0200, 0x0200, 0x0068 };

		assert(!starbook_mtl_dma_collect_facts(&io, 4, duplicate,
			0x800000, 0x200000, &facts));
	}
	assert(!starbook_mtl_dma_collect_facts(&io, 4, requesters,
		UINT64_MAX - 1U, 4, &facts));
	memset(mock.vendor, 0xff, sizeof(mock.vendor));
	memset(mock.command, 0, sizeof(mock.command));
	for (size_t devfn = 0; devfn < 256; devfn++) {
		mock.vendor[0][devfn] = 0x8086;
		mock.command[0][devfn] = 4;
	}
	assert(starbook_mtl_dma_collect_facts(&io, 4, requesters,
		0x800000, 0x200000, &facts));
	assert(facts.bus_master_count == STARBOOK_MTL_DMA_BME_MAX);
	assert(facts.bus_master_bdf[STARBOOK_MTL_DMA_BME_MAX - 1] == 0x00ff);
	mock.vendor[1][0] = 0x8086;
	mock.command[1][0] = 4;
	assert(!starbook_mtl_dma_collect_facts(&io, 4, requesters,
		0x800000, 0x200000, &facts));
	return 0;
}
