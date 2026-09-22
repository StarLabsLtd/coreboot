/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/soc/intel/common/block/vtd/vtd_translation.h"

#define TEST_PAGES 32U
#define PRESENT 1ULL
#define ADDRESS_MASK 0x000ffffffffff000ULL

static uint8_t *memory;

static uint64_t *table(uint64_t physical)
{
	return (void *)(memory + (physical - 0x100000U));
}

static uint64_t translate(uint16_t bdf, uint64_t device)
{
	uint64_t entry;
	uint64_t *next = (void *)memory;

	entry = next[(size_t)(bdf >> 8) * 2U];
	assert(entry & PRESENT);
	next = table(entry & ADDRESS_MASK);
	entry = next[(size_t)(bdf & 0xffU) * 2U];
	assert(entry & PRESENT);
	next = table(entry & ADDRESS_MASK);
	entry = next[(device >> 39) & 0x1ffU];
	assert(entry & PRESENT);
	next = table(entry & ADDRESS_MASK);
	entry = next[(device >> 30) & 0x1ffU];
	assert(entry & PRESENT);
	next = table(entry & ADDRESS_MASK);
	entry = next[(device >> 21) & 0x1ffU];
	assert(entry & PRESENT);
	next = table(entry & ADDRESS_MASK);
	return next[(device >> 12) & 0x1ffU];
}

int main(void)
{
	struct vtd_translation_image image;
	struct vtd_translation_requester requesters[] = {
		{ .bdf = 0x0200, .domain = 1, .cpu_base = 0x400000,
		  .device_base = 0x1ff000, .pages = 32 },
		{ .bdf = 0x00a0, .domain = 2, .cpu_base = 0x800000,
		  .device_base = 0x80000000, .pages = 128 },
		{ .bdf = 0x0068, .domain = 3, .cpu_base = 0x900000,
		  .device_base = 0x90000000, .pages = 128 },
	};
	void *allocation;

	assert(!posix_memalign(&allocation, 4096, TEST_PAGES * 4096U));
	memory = allocation;
	memset(memory, 0xa5, TEST_PAGES * 4096U);
	image = (struct vtd_translation_image) {
		.memory = memory,
		.physical_base = 0x100000,
		.capacity_pages = TEST_PAGES,
	};
	assert(!vtd_translation_build(&image, requesters, 3));
	assert(image.used_pages == 16U);
	assert(((uint64_t *)memory)[0] == 0x107001);
	assert(((uint64_t *)memory)[2U * 2U] == 0x101001);
	assert(table(0x101000)[0] == 0x102001);
	assert(table(0x101000)[1] == 0x102);
	assert(table(0x107000)[0xa0U * 2U] == 0x108001);
	assert(table(0x107000)[0xa0U * 2U + 1U] == 0x202);
	assert(table(0x107000)[0x68U * 2U] == 0x10c001);
	assert(table(0x107000)[0x68U * 2U + 1U] == 0x302);
	assert(table(0x102000)[0] == 0x103003);
	assert(table(0x103000)[0] == 0x104003);
	assert(table(0x104000)[0] == 0x105003);
	assert(table(0x104000)[1] == 0x106003);
	assert(table(0x105000)[0x1ffU] == 0x400003);
	assert(table(0x106000)[0] == 0x401003);
	assert(table(0x108000)[0] == 0x109003);
	assert(table(0x109000)[2] == 0x10a003);
	assert(table(0x10a000)[0] == 0x10b003);
	assert(table(0x10b000)[0] == 0x800003);
	assert(table(0x10b000)[127] == 0x87f003);
	assert((translate(0x0200, 0x1ff000) & ADDRESS_MASK) == 0x400000);
	assert((translate(0x0200, 0x200000) & ADDRESS_MASK) == 0x401000);
	assert((translate(0x00a0, 0x8007f000) & ADDRESS_MASK) == 0x87f000);
	assert(!(translate(0x0068, 0x90080000) & PRESENT));
	assert(!(memory[(0x03U * 16U)] & PRESENT));

	requesters[1].domain = requesters[0].domain;
	assert(vtd_translation_build(&image, requesters, 3));
	requesters[1].domain = 2;
	requesters[1].cpu_base = requesters[0].cpu_base;
	assert(vtd_translation_build(&image, requesters, 3));
	requesters[1].cpu_base = 0x800000;
	requesters[1].bdf = requesters[0].bdf;
	assert(vtd_translation_build(&image, requesters, 3));
	requesters[1].bdf = 0x00a0;
	image.capacity_pages = 4;
	assert(vtd_translation_build(&image, requesters, 3));
	image.capacity_pages = TEST_PAGES;
	requesters[1].cpu_base = 0x110000;
	assert(vtd_translation_build(&image, requesters, 3));
	free(allocation);
	return 0;
}
