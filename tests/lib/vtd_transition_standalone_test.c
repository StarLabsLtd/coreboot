/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../../src/soc/intel/common/block/vtd/vtd_transition.h"

#define CAP 0x08U
#define ECAP 0x10U
#define GCMD 0x18U
#define GSTS 0x1cU
#define RTADDR 0x20U
#define CCMD 0x28U
#define PMEN 0x64U
#define IOTLB 0x108U

struct mock {
	uint32_t registers[0x200 / sizeof(uint32_t)];
	bool commit;
	uint32_t drop_offset;
	uint32_t drop_value;
	uint32_t corrupt_root_after_translation;
};

static uint32_t mock_read(void *context, uint32_t offset)
{
	struct mock *mock = context;

	return mock->registers[offset / sizeof(uint32_t)];
}

static void mock_write(void *context, uint32_t offset, uint32_t value)
{
	struct mock *mock = context;

	if (offset == mock->drop_offset &&
	    (!mock->drop_value || value == mock->drop_value))
		return;
	mock->registers[offset / sizeof(uint32_t)] = value;
	if (offset == PMEN)
		mock->registers[PMEN / 4U] &= ~1U;
	else if (offset == GCMD && value == (1U << 30))
		mock->registers[GSTS / 4U] |= 1U << 30;
	else if (offset == GCMD && value == (1U << 31))
		mock->registers[GSTS / 4U] |= 1U << 31;
	else if (offset == CCMD + 4U && value & (1U << 31))
		mock->registers[offset / 4U] = 1U << 27;
	else if (offset == IOTLB + 4U && value & (1U << 31))
		mock->registers[offset / 4U] = 1U << 25;
	if (offset == GCMD && value == (1U << 31) &&
	    mock->corrupt_root_after_translation)
		mock->registers[RTADDR / 4U] ^= 0x1000U;
}

static void mock_commit(void *context)
{
	((struct mock *)context)->commit = true;
}

static struct mock valid_mock(void)
{
	struct mock mock = { 0 };

	mock.registers[0] = 0x10;
	mock.registers[CAP / 4U] = 1U << 10;
	mock.registers[ECAP / 4U] = 0x10U << 8 | 1U;
	mock.registers[PMEN / 4U] = 1U << 31 | 1U;
	return mock;
}

int main(void)
{
	struct mock mock = valid_mock();
	const struct vtd_transition_io io = {
		.context = &mock,
		.read32 = mock_read,
		.write32 = mock_write,
		.commit_tables = mock_commit,
	};
	struct vtd_transition_facts facts;

	assert(!vtd_transition_probe(&io, &facts));
	assert(facts.coherent);
	assert(facts.iotlb_offset == IOTLB);
	assert(!vtd_transition_from_pmr(&io, 0x100000));
	assert(mock.commit);
	assert(mock.registers[RTADDR / 4U] == 0x100000);
	assert((mock.registers[GSTS / 4U] & (3U << 30)) == (3U << 30));
	assert(!(mock.registers[PMEN / 4U] & 1U));

	mock = valid_mock();
	mock.registers[PMEN / 4U] = 0;
	assert(vtd_transition_from_pmr(&io, 0x100000));
	assert(!mock.commit);
	mock = valid_mock();
	mock.registers[GSTS / 4U] = 1U << 31;
	assert(vtd_transition_from_pmr(&io, 0x100000));
	assert(!mock.commit);
	mock = valid_mock();
	mock.drop_offset = RTADDR;
	assert(vtd_transition_from_pmr(&io, 0x100000) == -2);
	assert(mock.registers[PMEN / 4U] & 1U);
	mock = valid_mock();
	mock.drop_offset = GCMD;
	mock.drop_value = 1U << 30;
	assert(vtd_transition_from_pmr(&io, 0x100000) == -3);
	assert(mock.registers[PMEN / 4U] & 1U);
	mock = valid_mock();
	mock.drop_offset = CCMD + 4U;
	assert(vtd_transition_from_pmr(&io, 0x100000) == -4);
	assert(mock.registers[PMEN / 4U] & 1U);
	mock = valid_mock();
	mock.drop_offset = IOTLB + 4U;
	assert(vtd_transition_from_pmr(&io, 0x100000) == -4);
	assert(mock.registers[PMEN / 4U] & 1U);
	mock = valid_mock();
	mock.drop_offset = GCMD;
	mock.drop_value = 1U << 31;
	assert(vtd_transition_from_pmr(&io, 0x100000) == -5);
	assert(mock.registers[PMEN / 4U] & 1U);
	mock = valid_mock();
	mock.corrupt_root_after_translation = 1;
	assert(vtd_transition_from_pmr(&io, 0x100000) == -6);
	assert(mock.registers[PMEN / 4U] & 1U);
	mock = valid_mock();
	mock.drop_offset = PMEN;
	assert(vtd_transition_from_pmr(&io, 0x100000) == -7);
	assert(mock.registers[PMEN / 4U] & 1U);
	assert(mock.commit);
	mock = valid_mock();
	mock.registers[CAP / 4U] = 0;
	assert(vtd_transition_probe(&io, &facts));
	mock = valid_mock();
	mock.registers[ECAP / 4U] &= ~1U;
	assert(vtd_transition_probe(&io, &facts) == 0);
	assert(!facts.coherent);
	assert(vtd_transition_from_pmr(&io, 0x100000) == -1);
	assert(mock.registers[PMEN / 4U] & 1U);
	return 0;
}
