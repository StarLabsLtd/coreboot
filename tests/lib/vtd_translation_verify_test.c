/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/soc/intel/common/block/vtd/vtd_translation.h"

#define PHYSICAL_BASE 0x100000U
#define CAPACITY_PAGES 128U
#define PAGE_SIZE VTD_TRANSLATION_PAGE_SIZE

struct reader_context {
	uint8_t *memory;
	size_t size;
	uint64_t fail_address;
};

static int read64(void *opaque, uint64_t address, uint64_t *value)
{
	struct reader_context *context = opaque;
	uint64_t offset;

	if (!value || address == context->fail_address || address < PHYSICAL_BASE)
		return -1;
	offset = address - PHYSICAL_BASE;
	if (offset & (sizeof(*value) - 1U) ||
	    offset > context->size - sizeof(*value))
		return -1;
	memcpy(value, context->memory + offset, sizeof(*value));
	return 0;
}

static uint64_t *entry(uint8_t *memory, size_t page, size_t index)
{
	return (void *)(memory + page * PAGE_SIZE + index * sizeof(uint64_t));
}

static void restore(uint8_t *memory, const uint8_t *pristine)
{
	memcpy(memory, pristine, CAPACITY_PAGES * PAGE_SIZE);
}

static uint32_t random32(uint32_t *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return *state;
}

static void hostile_tests(uint8_t *memory, uint8_t *pristine,
	struct vtd_translation_image *image,
	struct vtd_translation_view *view,
	struct vtd_translation_requester requesters[3])
{
	struct reader_context *reader = view->context;
	const size_t used = image->used_pages;

	assert(!vtd_translation_verify(view, requesters, 3));

	*entry(memory, 0, 6) = PHYSICAL_BASE | 1U;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 0, 1) = 1;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 1, 2) = PHYSICAL_BASE | 1U;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 1, 1) ^= 0x100U;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 1, 0) |= 2U;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 2, 1) = PHYSICAL_BASE | 3U;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 3, 0) |= 0x80U;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 3, 0) = (PHYSICAL_BASE + 3U * PAGE_SIZE) | 3U;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 4, 1) = *entry(memory, 4, 0);
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 5, 0x1ffU) ^= PAGE_SIZE;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, 2, 0) = *entry(memory, 2, 0) - PAGE_SIZE;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);
	*entry(memory, used, 0) = 1;
	assert(vtd_translation_verify(view, requesters, 3));
	restore(memory, pristine);

	view->used_pages--;
	assert(vtd_translation_verify(view, requesters, 3));
	view->used_pages = used + 1U;
	assert(vtd_translation_verify(view, requesters, 3));
	view->used_pages = used;
	reader->fail_address = PHYSICAL_BASE;
	assert(vtd_translation_verify(view, requesters, 3));
	reader->fail_address = UINT64_MAX;
	view->capacity_pages = VTD_TRANSLATION_VERIFY_MAX_PAGES + 1U;
	assert(vtd_translation_verify(view, requesters, 3));
	view->capacity_pages = CAPACITY_PAGES;
	assert(vtd_translation_verify(view, requesters,
		VTD_TRANSLATION_VERIFY_MAX_REQUESTERS + 1U));

	requesters[1].domain = requesters[0].domain;
	assert(vtd_translation_verify(view, requesters, 3));
	requesters[1].domain = 2;
	requesters[1].bdf = requesters[0].bdf;
	assert(vtd_translation_verify(view, requesters, 3));
	requesters[1].bdf = 0x00a0;
	requesters[1].cpu_base = requesters[0].cpu_base;
	assert(vtd_translation_verify(view, requesters, 3));
	requesters[1].cpu_base = 0x800000;
	requesters[0].device_base += PAGE_SIZE;
	assert(vtd_translation_verify(view, requesters, 3));
	requesters[0].device_base -= PAGE_SIZE;
}

static void boundary_parity(uint8_t *memory, struct reader_context *reader)
{
	static const uint64_t boundaries[] = {
		1ULL << 21,
		1ULL << 30,
		1ULL << 39,
	};

	for (size_t index = 0; index < 3U; index++) {
		struct vtd_translation_requester requester = {
			.bdf = 0x0100,
			.domain = 1,
			.cpu_base = 0x100000000ULL,
			.device_base = boundaries[index] - PAGE_SIZE,
			.pages = 2,
		};
		struct vtd_translation_image image = {
			.memory = memory,
			.physical_base = PHYSICAL_BASE,
			.capacity_pages = CAPACITY_PAGES,
		};
		struct vtd_translation_view view = {
			.context = reader,
			.physical_base = PHYSICAL_BASE,
			.capacity_pages = CAPACITY_PAGES,
			.read64 = read64,
		};

		memset(memory, 0, CAPACITY_PAGES * PAGE_SIZE);
		assert(!vtd_translation_build(&image, &requester, 1));
		view.used_pages = image.used_pages;
		assert(!vtd_translation_verify(&view, &requester, 1));
	}
}

static void randomized_parity(uint8_t *memory,
	struct reader_context *reader)
{
	uint32_t random = 0x51a7c0deU;

	for (size_t iteration = 0; iteration < 512U; iteration++) {
		struct vtd_translation_requester requesters[3];
		struct vtd_translation_image image = {
			.memory = memory,
			.physical_base = PHYSICAL_BASE,
			.capacity_pages = CAPACITY_PAGES,
		};
		struct vtd_translation_view view = {
			.context = reader,
			.physical_base = PHYSICAL_BASE,
			.capacity_pages = CAPACITY_PAGES,
			.read64 = read64,
		};

		memset(memory, 0, CAPACITY_PAGES * PAGE_SIZE);
		for (size_t index = 0; index < 3U; index++) {
			const uint32_t pages = random32(&random) % 192U + 1U;
			const uint64_t device_page =
				((uint64_t)(random32(&random) & 0xfffffU) +
				 index * 0x200000U);

			requesters[index] = (struct vtd_translation_requester) {
				.bdf = (uint16_t)((index == 2U ? 0x20U : 0x10U) << 8 |
					(random32(&random) & 0xffU)),
				.domain = index + 1U,
				.cpu_base = 0x100000000ULL +
					(uint64_t)index * 0x40000000ULL,
				.device_base = device_page <<
					VTD_TRANSLATION_PAGE_SHIFT,
				.pages = pages,
			};
		}
		if (requesters[0].bdf == requesters[1].bdf)
			requesters[1].bdf ^= 1U;
		assert(!vtd_translation_build(&image, requesters, 3));
		view.used_pages = image.used_pages;
		assert(!vtd_translation_verify(&view, requesters, 3));
	}
}

int main(void)
{
	struct vtd_translation_requester requesters[3] = {
		{ .bdf = 0x0200, .domain = 1, .cpu_base = 0x400000,
		  .device_base = 0x1ff000, .pages = 32 },
		{ .bdf = 0x00a0, .domain = 2, .cpu_base = 0x800000,
		  .device_base = 0x80000000, .pages = 128 },
		{ .bdf = 0x0068, .domain = 3, .cpu_base = 0x900000,
		  .device_base = 0x90000000, .pages = 128 },
	};
	struct vtd_translation_image image;
	struct vtd_translation_view view;
	struct reader_context reader;
	uint8_t *memory;
	uint8_t *pristine;

	assert(!posix_memalign((void **)&memory, PAGE_SIZE,
		CAPACITY_PAGES * PAGE_SIZE));
	pristine = malloc(CAPACITY_PAGES * PAGE_SIZE);
	assert(pristine);
	memset(memory, 0, CAPACITY_PAGES * PAGE_SIZE);
	image = (struct vtd_translation_image) {
		.memory = memory,
		.physical_base = PHYSICAL_BASE,
		.capacity_pages = CAPACITY_PAGES,
	};
	assert(!vtd_translation_build(&image, requesters, 3));
	memcpy(pristine, memory, CAPACITY_PAGES * PAGE_SIZE);
	reader = (struct reader_context) {
		.memory = memory,
		.size = CAPACITY_PAGES * PAGE_SIZE,
		.fail_address = UINT64_MAX,
	};
	view = (struct vtd_translation_view) {
		.context = &reader,
		.physical_base = PHYSICAL_BASE,
		.capacity_pages = CAPACITY_PAGES,
		.used_pages = image.used_pages,
		.read64 = read64,
	};
	hostile_tests(memory, pristine, &image, &view, requesters);
	boundary_parity(memory, &reader);
	randomized_parity(memory, &reader);
	free(pristine);
	free(memory);
	return 0;
}
