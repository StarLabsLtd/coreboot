/* SPDX-License-Identifier: GPL-2.0-only */
/* Load an IA32 rmodule from a 64-bit caller without widening its fixups. */
#include <stdint.h>
#include <stddef.h>
#include <commonlib/bsd/compiler.h>
#include "loader.h"
#include <commonlib/rmodule-defs.h>

static uint32_t read_le32(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le32(uint8_t *p, uint32_t value)
{
	for (unsigned int i = 0; i < 4; i++)
		p[i] = value >> (8 * i);
}

static int within(uint32_t address, uint32_t length, uint32_t base, uint32_t size)
{
	return address >= base && address - base <= size && length <= size - (address - base);
}

uintptr_t amd_pei_gop_load_rmodule(const void *blob, size_t length, void *destination,
				   size_t capacity)
{
	const struct rmodule_header *h = blob;
	const uint8_t *source = blob;
	uint8_t *image = destination;
	uintptr_t target = (uintptr_t)destination, input = (uintptr_t)blob;
	if (!blob || !destination || length < sizeof(*h) || length > (~(uintptr_t)0) - input ||
	    h->magic != RMODULE_MAGIC || h->version != RMODULE_VERSION_1)
		return 0;
	if (h->payload_begin_offset < sizeof(*h) ||
	    h->payload_end_offset < h->payload_begin_offset ||
	    h->relocations_begin_offset < h->payload_end_offset ||
	    h->relocations_end_offset < h->relocations_begin_offset ||
	    h->relocations_end_offset > length ||
	    (h->relocations_end_offset - h->relocations_begin_offset) % 4)
		return 0;
	uint32_t size = h->module_program_size;
	uint32_t payload = h->payload_end_offset - h->payload_begin_offset;
	uint32_t link = h->module_link_start_address;
	if (!size || size > capacity || payload > size || target > UINT32_MAX - size ||
	    link > UINT32_MAX - size || (target < input + length && input < target + size) ||
	    !within(h->module_entry_point, 1, link, payload) || h->bss_end < h->bss_begin ||
	    !within(h->bss_begin, h->bss_end - h->bss_begin, link, size) ||
	    h->bss_begin - link < payload)
		return 0;
	if (h->parameters_end != h->parameters_begin &&
	    (h->parameters_end < h->parameters_begin ||
	     !within(h->parameters_begin, h->parameters_end - h->parameters_begin, link, size)))
		return 0;
	/* Validate all four-byte fixup slots before copying or clearing any memory. */
	for (size_t off = h->relocations_begin_offset; off < h->relocations_end_offset;
	     off += 4)
		if (!within(read_le32(source + off), 4, link, payload))
			return 0;
	for (size_t i = 0; i < size; i++)
		image[i] = 0;
	for (size_t i = 0; i < payload; i++)
		image[i] = source[h->payload_begin_offset + i];
	const uint32_t delta = (uint32_t)target - link;
	for (size_t off = h->relocations_begin_offset; off < h->relocations_end_offset;
	     off += 4) {
		uint8_t *slot = image + (read_le32(source + off) - link);
		write_le32(slot, read_le32(slot) + delta);
	}
	return target + h->module_entry_point - link;
}
