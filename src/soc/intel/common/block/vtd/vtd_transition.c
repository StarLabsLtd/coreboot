/* SPDX-License-Identifier: GPL-2.0-only */

#include "vtd_transition.h"

#include <stddef.h>

#define VTD_VERSION 0x00U
#define VTD_CAPABILITY 0x08U
#define VTD_EXTENDED_CAPABILITY 0x10U
#define VTD_GLOBAL_COMMAND 0x18U
#define VTD_GLOBAL_STATUS 0x1cU
#define VTD_ROOT_ADDRESS 0x20U
#define VTD_CONTEXT_COMMAND 0x28U
#define VTD_PROTECTED_MEMORY_ENABLE 0x64U

#define VTD_ROOT_POINTER_SET (1U << 30)
#define VTD_TRANSLATION_ENABLE (1U << 31)
#define VTD_PROTECTED_MEMORY_ACTIVE (1U << 0)
#define VTD_PROTECTED_MEMORY_REQUEST (1U << 31)
#define VTD_INVALIDATE (1U << 31)
#define VTD_CONTEXT_GLOBAL (1U << 29)
#define VTD_IOTLB_GLOBAL (1U << 28)
#define VTD_CAP_SAGAW_48 (1ULL << 10)
#define VTD_ECAP_COHERENT (1ULL << 0)
#define VTD_POLL_LIMIT 100000U

static uint64_t read64(const struct vtd_transition_io *io, uint32_t offset)
{
	return io->read32(io->context, offset) |
		((uint64_t)io->read32(io->context, offset + 4U) << 32);
}

static void write64(const struct vtd_transition_io *io, uint32_t offset,
	uint64_t value)
{
	io->write32(io->context, offset, value);
	io->write32(io->context, offset + 4U, value >> 32);
}

static int wait32(const struct vtd_transition_io *io, uint32_t offset,
	uint32_t mask, uint32_t expected)
{
	for (unsigned int poll = 0; poll < VTD_POLL_LIMIT; poll++)
		if ((io->read32(io->context, offset) & mask) == expected)
			return 0;
	return -1;
}

int vtd_transition_probe(const struct vtd_transition_io *io,
	struct vtd_transition_facts *facts)
{
	uint32_t iotlb;

	if (!io || !facts || !io->read32 || !io->write32 || !io->commit_tables)
		return -1;
	*facts = (struct vtd_transition_facts) {
		.version = io->read32(io->context, VTD_VERSION),
		.capability = read64(io, VTD_CAPABILITY),
		.extended_capability = read64(io, VTD_EXTENDED_CAPABILITY),
		.status = io->read32(io->context, VTD_GLOBAL_STATUS),
		.root = read64(io, VTD_ROOT_ADDRESS),
		.protected_memory_enable = io->read32(io->context,
			VTD_PROTECTED_MEMORY_ENABLE),
	};
	iotlb = (uint32_t)((facts->extended_capability >> 8) & 0x3ffU) * 16U + 8U;
	if (!facts->version || facts->version == UINT32_MAX ||
	    !(facts->capability & VTD_CAP_SAGAW_48) ||
	    iotlb < 0x40U || iotlb > 0xff0U)
		return -1;
	facts->iotlb_offset = iotlb;
	facts->coherent = !!(facts->extended_capability & VTD_ECAP_COHERENT);
	return 0;
}

static int invalidate(const struct vtd_transition_io *io,
	const struct vtd_transition_facts *facts)
{
	write64(io, VTD_CONTEXT_COMMAND,
		(uint64_t)VTD_INVALIDATE << 32 | (uint64_t)VTD_CONTEXT_GLOBAL << 32);
	if (wait32(io, VTD_CONTEXT_COMMAND + 4U, VTD_INVALIDATE, 0) ||
	    (io->read32(io->context, VTD_CONTEXT_COMMAND + 4U) & (3U << 27)) !=
		(1U << 27))
		return -1;
	write64(io, facts->iotlb_offset,
		(uint64_t)VTD_INVALIDATE << 32 | (uint64_t)VTD_IOTLB_GLOBAL << 32);
	if (wait32(io, facts->iotlb_offset + 4U, VTD_INVALIDATE, 0) ||
	    (io->read32(io->context, facts->iotlb_offset + 4U) & (3U << 25)) !=
		(1U << 25))
		return -1;
	return 0;
}

int vtd_transition_from_pmr(const struct vtd_transition_io *io,
	uint64_t root_physical)
{
	struct vtd_transition_facts facts;
	uint32_t protected_memory;

	if (vtd_transition_probe(io, &facts) || !root_physical ||
	    (root_physical & 0xfffU) || root_physical >= (1ULL << 48) ||
	    (facts.status & (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) ||
	    !(facts.protected_memory_enable & VTD_PROTECTED_MEMORY_ACTIVE) ||
	    !facts.coherent)
		return -1;

	io->commit_tables(io->context);
	write64(io, VTD_ROOT_ADDRESS, root_physical);
	if (read64(io, VTD_ROOT_ADDRESS) != root_physical)
		return -2;
	io->write32(io->context, VTD_GLOBAL_COMMAND, VTD_ROOT_POINTER_SET);
	if (wait32(io, VTD_GLOBAL_STATUS, VTD_ROOT_POINTER_SET,
		VTD_ROOT_POINTER_SET))
		return -3;
	if (invalidate(io, &facts))
		return -4;
	io->write32(io->context, VTD_GLOBAL_COMMAND, VTD_TRANSLATION_ENABLE);
	if (wait32(io, VTD_GLOBAL_STATUS, VTD_TRANSLATION_ENABLE,
		VTD_TRANSLATION_ENABLE))
		return -5;
	if (read64(io, VTD_ROOT_ADDRESS) != root_physical ||
	    (io->read32(io->context, VTD_GLOBAL_STATUS) &
	     (VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE)) !=
		(VTD_ROOT_POINTER_SET | VTD_TRANSLATION_ENABLE))
		return -6;

	/* PMR remains active until translation and its root are both proven. */
	protected_memory = facts.protected_memory_enable &
		~VTD_PROTECTED_MEMORY_REQUEST;
	io->write32(io->context, VTD_PROTECTED_MEMORY_ENABLE, protected_memory);
	if (wait32(io, VTD_PROTECTED_MEMORY_ENABLE,
		VTD_PROTECTED_MEMORY_ACTIVE, 0))
		return -7;
	return 0;
}
