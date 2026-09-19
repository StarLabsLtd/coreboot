/* SPDX-License-Identifier: GPL-2.0-only */

#include "vtd_registers.h"

#define VTD_POLL_LIMIT 100000U
#define VTD_INVALIDATE (1U << 31)
#define VTD_CONTEXT_GLOBAL (1U << 29)
#define VTD_IOTLB_GLOBAL (1U << 28)

static uint64_t read_pair(const struct q35_vtd_io *io, uint32_t offset)
{
	uint32_t low = io->read32(io->context, offset);
	uint32_t high = io->read32(io->context, offset + 4U);

	return ((uint64_t)high << 32) | low;
}

static int wait_for(const struct q35_vtd_io *io, uint32_t offset,
	uint32_t mask, uint32_t expected)
{
	for (unsigned int poll = 0; poll < VTD_POLL_LIMIT; poll++)
		if ((io->read32(io->context, offset) & mask) == expected)
			return 0;
	return -1;
}

static int invalidate(const struct q35_vtd_io *io, uint32_t iotlb)
{
	io->write32(io->context, Q35_VTD_CCMD, 0);
	io->write32(io->context, Q35_VTD_CCMD + 4U,
		VTD_INVALIDATE | VTD_CONTEXT_GLOBAL);
	if (wait_for(io, Q35_VTD_CCMD + 4U, VTD_INVALIDATE, 0) ||
	    (io->read32(io->context, Q35_VTD_CCMD + 4U) & (3U << 27)) !=
		(1U << 27))
		return -1;

	io->write32(io->context, iotlb, 0);
	io->write32(io->context, iotlb + 4U, VTD_INVALIDATE | VTD_IOTLB_GLOBAL);
	if (wait_for(io, iotlb + 4U, VTD_INVALIDATE, 0) ||
	    (io->read32(io->context, iotlb + 4U) & (3U << 25)) !=
		(1U << 25))
		return -1;
	return 0;
}

static int iotlb_offset(const struct q35_vtd_io *io, uint32_t *iotlb)
{
	uint64_t ecap = read_pair(io, Q35_VTD_ECAP);

	*iotlb = (uint32_t)((ecap >> 8) & 0x3ffU) * 16U + 8U;
	return *iotlb < 0x40U || *iotlb > 0xff0U ? -1 : 0;
}

int q35_vtd_default_deny(const struct q35_vtd_io *io, uint32_t root_phys)
{
	uint32_t iotlb;
	uint32_t version;

	if (!io || !io->read32 || !io->write32 || !io->commit_tables ||
	    !root_phys || (root_phys & 0xfffU))
		return -1;
	version = io->read32(io->context, Q35_VTD_VERSION);
	if (version == UINT32_MAX || ((version >> 4) & 0xfU) != 1U)
		return -2;
	if (io->read32(io->context, Q35_VTD_GSTS) &
	    (Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE))
		return -3;
	if (iotlb_offset(io, &iotlb))
		return -4;

	io->commit_tables(io->context);
	io->write32(io->context, Q35_VTD_RTADDR, root_phys);
	io->write32(io->context, Q35_VTD_RTADDR + 4U, 0);
	if (read_pair(io, Q35_VTD_RTADDR) != root_phys)
		return -5;
	io->write32(io->context, Q35_VTD_GCMD, Q35_VTD_ROOT_SET);
	if (wait_for(io, Q35_VTD_GSTS, Q35_VTD_ROOT_SET, Q35_VTD_ROOT_SET))
		return -6;
	if (invalidate(io, iotlb))
		return -7;
	io->write32(io->context, Q35_VTD_GCMD, Q35_VTD_TRANSLATION_ENABLE);
	if (wait_for(io, Q35_VTD_GSTS, Q35_VTD_TRANSLATION_ENABLE,
		Q35_VTD_TRANSLATION_ENABLE))
		return -8;
	return (io->read32(io->context, Q35_VTD_GSTS) &
		Q35_VTD_TRANSLATION_ENABLE) ? 0 : -9;
}
