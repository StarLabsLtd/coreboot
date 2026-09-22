/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_VTD_TRANSLATION_H
#define SOC_INTEL_COMMON_BLOCK_VTD_TRANSLATION_H

#include <stddef.h>
#include <stdint.h>

#define VTD_TRANSLATION_PAGE_SHIFT 12U
#define VTD_TRANSLATION_PAGE_SIZE (1U << VTD_TRANSLATION_PAGE_SHIFT)

struct vtd_translation_requester {
	uint16_t segment;
	uint16_t bdf;
	uint16_t domain;
	uint64_t cpu_base;
	uint64_t device_base;
	uint32_t pages;
};

struct vtd_translation_image {
	void *memory;
	uint64_t physical_base;
	size_t capacity_pages;
	size_t used_pages;
};

int vtd_translation_build(struct vtd_translation_image *image,
	const struct vtd_translation_requester *requesters, size_t requester_count);

#endif
