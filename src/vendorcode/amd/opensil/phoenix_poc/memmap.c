/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <SilCommon.h>
#include <Sil-api.h> // needed above ApobCmn.h
#include <APOB/Common/ApobCmn.h>
#include <xPRF-api.h>

#include "../opensil.h"

_Static_assert(sizeof(uint32_t) == sizeof(((MEMORY_HOLE_DESCRIPTOR){0}).Type),
	"Unexpected size of MEMORY_HOLE_TYPES in the MEMORY_HOLE_DESCRIPTOR "
	"struct which doesn't match the code in drivers/amd/opensil/memmap.c");

const char *opensil_get_hole_info_type(uint32_t type)
{
	const struct hole_type {
		MEMORY_HOLE_TYPES type;
		const char *string;
	} types[] = {
		{UMA, "UMA"},
		{MMIO, "MMIO"},
		{PrivilegedDRAM, "PrivilegedDRAM"},
		{Reserved1TbRemap, "Reserved1TbRemap"},
		{ReservedSLink, "ReservedSLink"},
		{ReservedSLinkAlignment, "ReservedSLinkAlignment"},
		{ReservedDrtm, "ReservedDrtm"},
		{ReservedCvip, "ReservedCvip"},
		{ReservedSmuFeatures, "ReservedSmuFeatures"},
		{ReservedFwtpm, "ReservedFwtpm"},
		{ReservedMpioC20, "ReservedMpioC20"},
		{ReservedNbif, "ReservedNbif"},
		{ReservedCxl, "ReservedCxl"},
		{ReservedCxlAlignment, "ReservedCxlAlignment"},
		{ReservedCpuTmr, "ReservedCpuTmr"},
		{MaxMemoryHoleTypes, "MaxMemoryHoleTypes"},
	};

	int i;
	MEMORY_HOLE_TYPES enum_type = (MEMORY_HOLE_TYPES)type; // Cast int to enum
	for (i = 0; i < ARRAY_SIZE(types); i++)
		if (enum_type == types[i].type)
			return types[i].string;
	return "Unknown type";
}

static bool memory_holes_valid(const void *map, uint32_t count)
{
	const uintptr_t base = CONFIG_PSP_APOB_DRAM_ADDRESS;
	const size_t reservation = CONFIG_PSP_APOB_DRAM_SIZE;
	const size_t prefix = offsetof(APOB_SYSTEM_MEMORY_MAP_TYPE_STRUCT, ApobSystemMap.HoleInfo);
	const size_t capacity =
		ARRAY_SIZE(((APOB_SYSTEM_MEMORY_MAP_TYPE_STRUCT *)0)->ApobSystemMap.HoleInfo) +
		ARRAY_SIZE(((APOB_SYSTEM_MEMORY_MAP_TYPE_STRUCT *)0)->AdditionalHoleInfo);
	uintptr_t address = (uintptr_t)map;

	if (!map || !count || count > capacity || address < base ||
	    address - base < prefix || address - base > reservation ||
	    count * sizeof(MEMORY_HOLE_DESCRIPTOR) > reservation - (address - base))
		return false;

	address -= prefix;
	if (!IS_ALIGNED(address, _Alignof(APOB_SYSTEM_MEMORY_MAP_TYPE_STRUCT)))
		return false;
	const APOB_SYSTEM_MEMORY_MAP_TYPE_STRUCT *entry = (void *)address;
	const size_t length = entry->ApobTypeHeader.TypeSize;

	return length >= prefix + count * sizeof(MEMORY_HOLE_DESCRIPTOR) &&
		length <= reservation - (address - base);
}

void opensil_get_hole_info(uint32_t *n_holes, uint64_t *top_of_mem, void **hole_info)
{
	SIL_CONTEXT SilContext = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL)
	};

	if (!SilContext.SilMemBaseAddress)
		die("OpenSIL cbmem memory not found!\n");

	*n_holes = 0;
	*top_of_mem = 0;
	*hole_info = NULL;
	SIL_STATUS status = xPrfGetSystemMemoryMap(&SilContext, n_holes, top_of_mem, hole_info);
	SIL_STATUS_report("xPrfGetSystemMemoryMap", status);
	// Make sure hole_info does not get initialized to something odd by xPRF on failure
	if (status != SilPass || !memory_holes_valid(*hole_info, *n_holes)) {
		*hole_info = NULL;
		*n_holes = 0;
		*top_of_mem = 0;
	}
}

bool opensil_memory_region_is_ram(uint64_t base, uint64_t size)
{
	uint32_t count = 0;
	uint64_t top = 0;
	void *map = NULL;

	if (!size || size > UINT64_MAX - base)
		return false;
	opensil_get_hole_info(&count, &top, &map);
	if (!map || !count || base >= top || size > top - base)
		return false;

	const MEMORY_HOLE_DESCRIPTOR *holes = map;

	for (size_t i = 0; i < count; i++) {
		if (!holes[i].Size || holes[i].Size > UINT64_MAX - holes[i].Base)
			return false;
		/* Unknown hole types are reserved too. */
		if (base < holes[i].Base + holes[i].Size && holes[i].Base < base + size)
			return false;
	}
	return true;
}
