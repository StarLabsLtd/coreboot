/* SPDX-License-Identifier: GPL-2.0-only */
/* Bounded IA32 PE loader for the AMD PEI GOP host. */
#define EFIAPI __attribute__((cdecl, regparm(0)))
#include <Base.h>
#include "pe32.h"

static UINT16 r16(CONST UINT8 *p)
{
	return p[0] | ((UINT16)p[1] << 8);
}
static UINT32 r32(CONST UINT8 *p)
{
	return r16(p) | ((UINT32)r16(p + 2) << 16);
}
static void w32(UINT8 *p, UINT32 value)
{
	for (UINTN i = 0; i < 4; i++)
		p[i] = value >> (i * 8);
}
static BOOLEAN fits(UINTN off, UINTN size, UINTN total)
{
	return off <= total && size <= total - off;
}

UINTN load_pe32(CONST UINT8 *file, UINTN length, VOID *destination, UINTN capacity)
{
	UINT8 *image = destination;
	if (length < 64 || r16(file) != 0x5a4d)
		return 0;
	UINTN pe = r32(file + 60);
	if (!fits(pe, 24, length) || r32(file + pe) != 0x4550 || r16(file + pe + 4) != 0x14c)
		return 0;
	UINTN optional = pe + 24, optional_size = r16(file + pe + 20);
	if (optional_size < 96 + 6 * 8 || !fits(optional, optional_size, length) ||
	    r16(file + optional) != 0x10b || r32(file + optional + 92) <= 5)
		return 0;
	UINTN size = r32(file + optional + 56), headers = r32(file + optional + 60);
	UINTN entry = r32(file + optional + 16), sections = r16(file + pe + 6);
	UINTN section_table = optional + optional_size;
	if (!size || size > capacity || size > ~0U - (UINTN)destination || headers > size ||
	    headers > length || entry >= size || sections > (length - section_table) / 40)
		return 0;
	BOOLEAN executable_entry = FALSE;
	for (UINTN i = 0; i < sections; i++) {
		CONST UINT8 *s = file + section_table + i * 40;
		UINTN raw = r32(s + 16), virt = r32(s + 8), target = r32(s + 12);
		UINTN extent = raw > virt ? raw : virt;
		if (!fits(r32(s + 20), raw, length) || !fits(target, extent, size) ||
		    (extent && target < headers))
			return 0;
		for (UINTN j = 0; j < i; j++) {
			CONST UINT8 *other = file + section_table + j * 40;
			UINTN a = r32(other + 8), b = r32(other + 16);
			UINTN n = a > b ? a : b, begin = r32(other + 12);
			if (extent && n && target < begin + n && begin < target + extent)
				return 0;
		}
		if ((r32(s + 36) & 0x20000000) && entry >= target && entry - target < extent)
			executable_entry = TRUE;
	}
	if (!executable_entry)
		return 0;
	for (UINTN i = 0; i < size; i++)
		image[i] = 0;
	for (UINTN i = 0; i < headers; i++)
		image[i] = file[i];
	for (UINTN i = 0; i < sections; i++) {
		CONST UINT8 *s = file + section_table + i * 40;
		for (UINTN j = 0; j < r32(s + 16); j++)
			image[r32(s + 12) + j] = file[r32(s + 20) + j];
	}
	UINT32 delta = (UINTN)destination - r32(file + optional + 28);
	UINTN offset = r32(file + optional + 96 + 5 * 8);
	UINTN reloc_size = r32(file + optional + 96 + 5 * 8 + 4);
	if (!fits(offset, reloc_size, size) || (!reloc_size && delta))
		return 0;
	UINTN end = offset + reloc_size;
	while (offset < end) {
		if (!fits(offset, 8, end))
			return 0;
		UINTN page = r32(image + offset), block = r32(image + offset + 4);
		if (block < 8 || block % 2 || !fits(offset, block, end))
			return 0;
		for (UINTN i = 8; i < block; i += 2) {
			UINT16 relocation = r16(image + offset + i);
			UINTN type = relocation >> 12, low = relocation & 4095;
			if (!type)
				continue;
			if (type != 3 || page > ~0U - low || !fits(page + low, 4, size))
				return 0;
			w32(image + page + low, r32(image + page + low) + delta);
		}
		offset += block;
	}
	return (UINTN)destination + entry;
}
