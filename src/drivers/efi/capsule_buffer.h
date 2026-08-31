/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _EFI_CAPSULE_BUFFER_H_
#define _EFI_CAPSULE_BUFFER_H_

#include <commonlib/helpers.h>
#include <bootmem.h>
#include <memrange.h>

static inline bool efi_capsule_pick_buffer(const struct memranges *ranges,
	uint64_t data_size, uint64_t *base, uint64_t *size)
{
	const struct range_entry *entry;
	uint64_t rounded;
	uint64_t selected = 0;

	if (ranges == NULL || base == NULL || size == NULL || data_size == 0 ||
	    data_size > UINT64_MAX - (4 * KiB - 1))
		return false;
	rounded = ALIGN_UP(data_size, 4 * KiB);
	memranges_each_entry(entry, ranges) {
		uint64_t entry_base;
		uint64_t entry_size;

		if (range_entry_tag(entry) != BM_MEM_RAM)
			continue;
		entry_base = range_entry_base(entry);
		entry_size = range_entry_size(entry);
		if (entry_base >= 4ULL * GiB)
			break;
		if (entry_size > 4ULL * GiB - entry_base)
			entry_size = 4ULL * GiB - entry_base;
		if (entry_size >= rounded)
			selected = entry_base + entry_size - rounded;
	}
	if (selected == 0)
		return false;
	*base = selected;
	*size = rounded;
	return true;
}

#endif
