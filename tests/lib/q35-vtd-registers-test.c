/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vtd_registers.h"

#define IOTLB_HIGH 0xfcU

struct mock {
	uint32_t registers[0x1000 / 4];
	unsigned int sequence;
	unsigned int commit_order;
	unsigned int root_order;
	unsigned int context_order;
	unsigned int iotlb_order;
	unsigned int enable_order;
	unsigned int roots;
	unsigned int enables;
	uint32_t last_gcmd;
	bool block_root;
	bool block_enable;
	bool block_context;
	bool block_iotlb;
};

static uint32_t mock_read(void *context, uint32_t offset)
{
	struct mock *mock = context;

	return mock->registers[offset / 4U];
}

static void mock_write(void *context, uint32_t offset, uint32_t value)
{
	struct mock *mock = context;

	if (offset == Q35_VTD_GCMD) {
		const bool translation_enabled =
			mock->registers[Q35_VTD_GSTS / 4U] &
			Q35_VTD_TRANSLATION_ENABLE;

		mock->last_gcmd = value;
		if (value & Q35_VTD_ROOT_SET) {
			mock->roots++;
			mock->root_order = ++mock->sequence;
			if (!mock->block_root)
				mock->registers[Q35_VTD_GSTS / 4U] |= Q35_VTD_ROOT_SET;
		}
		if (!!(value & Q35_VTD_TRANSLATION_ENABLE) !=
		    translation_enabled) {
			mock->enables++;
			mock->enable_order = ++mock->sequence;
			if (!mock->block_enable) {
				if (value & Q35_VTD_TRANSLATION_ENABLE)
					mock->registers[Q35_VTD_GSTS / 4U] |=
						Q35_VTD_TRANSLATION_ENABLE;
				else
					mock->registers[Q35_VTD_GSTS / 4U] &=
						~Q35_VTD_TRANSLATION_ENABLE;
			}
		}
	} else if (offset == Q35_VTD_CCMD + 4U && (value & (1U << 31))) {
		mock->context_order = ++mock->sequence;
		if (!mock->block_context)
			value = 1U << 27;
	} else if (offset == IOTLB_HIGH && (value & (1U << 31))) {
		mock->iotlb_order = ++mock->sequence;
		if (!mock->block_iotlb)
			value = 1U << 25;
	}
	mock->registers[offset / 4U] = value;
}

static void mock_commit(void *context)
{
	struct mock *mock = context;

	mock->commit_order = ++mock->sequence;
}

static void setup(struct mock *mock)
{
	memset(mock, 0, sizeof(*mock));
	mock->registers[Q35_VTD_VERSION / 4U] = 0x10U;
	mock->registers[Q35_VTD_ECAP / 4U] = 0xf00f0aU;
}

static int check(bool condition, const char *message)
{
	if (condition)
		return 0;
	fprintf(stderr, "Q35 VT-d register test: %s\n", message);
	return 1;
}

int main(void)
{
	struct mock mock;
	struct q35_vtd_io io = {
		.context = &mock,
		.read32 = mock_read,
		.write32 = mock_write,
		.commit_tables = mock_commit,
	};
	int failures = 0;

	setup(&mock);
	failures += check(q35_vtd_default_deny(&io, 0x100000U) == 0,
		"default-deny registration failed");
	failures += check(mock.roots == 1 && mock.enables == 1,
		"root/enable ordering incomplete");
	failures += check(mock.commit_order < mock.root_order &&
		mock.root_order < mock.context_order &&
		mock.context_order < mock.iotlb_order &&
		mock.iotlb_order < mock.enable_order,
		"table visibility/root/invalidation/enable ordering is wrong");
	setup(&mock);
	failures += check(q35_vtd_invalidate(&io) == 0 &&
		mock.context_order && mock.iotlb_order &&
		mock.context_order < mock.iotlb_order,
		"explicit invalidation failed");
	setup(&mock);
	mock.block_context = true;
	failures += check(q35_vtd_invalidate(&io) == -1,
		"failed explicit context invalidation accepted");
	setup(&mock);
	mock.registers[Q35_VTD_ECAP / 4U] = 0;
	failures += check(q35_vtd_invalidate(&io) == -1,
		"invalid explicit IOTLB offset accepted");
	setup(&mock);
	mock.registers[Q35_VTD_VERSION / 4U] = UINT32_MAX;
	failures += check(q35_vtd_default_deny(&io, 0x100000U) == -2,
		"missing VT-d device accepted");
	setup(&mock);
	mock.registers[Q35_VTD_GSTS / 4U] = Q35_VTD_TRANSLATION_ENABLE;
	failures += check(q35_vtd_default_deny(&io, 0x100000U) == -3,
		"foreign VT-d owner overwritten");
	setup(&mock);
	failures += check(q35_vtd_default_deny(&io, 0x100001U) == -1,
		"misaligned root accepted");
	setup(&mock);
	mock.block_root = true;
	failures += check(q35_vtd_default_deny(&io, 0x100000U) == -6 && !mock.enables,
		"missing root read-back still enabled translation");
	setup(&mock);
	mock.block_enable = true;
	failures += check(q35_vtd_default_deny(&io, 0x100000U) == -8,
		"missing translation read-back accepted");
	setup(&mock);
	mock.block_context = true;
	failures += check(q35_vtd_default_deny(&io, 0x100000U) == -7 &&
		!mock.enables, "failed context invalidation still enabled translation");
	setup(&mock);
	mock.block_iotlb = true;
	failures += check(q35_vtd_default_deny(&io, 0x100000U) == -7 &&
		!mock.enables, "failed IOTLB invalidation still enabled translation");
	setup(&mock);
	mock.registers[Q35_VTD_GSTS / 4U] =
		Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE;
	mock.registers[Q35_VTD_RTADDR / 4U] = 0x100000U;
	failures += check(q35_vtd_switch_root(&io, 0x100000U, 0x200000U) == 0,
		"active root replacement failed");
	failures += check(mock.last_gcmd ==
		(Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE) &&
		(mock.registers[Q35_VTD_GSTS / 4U] &
		 Q35_VTD_TRANSLATION_ENABLE),
		"root replacement did not preserve translation");
	failures += check(mock.commit_order < mock.root_order &&
		mock.root_order < mock.context_order &&
		mock.context_order < mock.iotlb_order,
		"replacement commit/root/invalidation ordering is wrong");
	setup(&mock);
	mock.registers[Q35_VTD_GSTS / 4U] =
		Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE;
	mock.registers[Q35_VTD_RTADDR / 4U] = 0x100000U;
	mock.block_context = true;
	failures += check(q35_vtd_switch_root(&io, 0x100000U, 0x200000U) == -5 &&
		(mock.registers[Q35_VTD_GSTS / 4U] &
		 Q35_VTD_TRANSLATION_ENABLE),
		"replacement invalidation failure disabled translation");
	setup(&mock);
	mock.registers[Q35_VTD_GSTS / 4U] =
		Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE;
	mock.registers[Q35_VTD_RTADDR / 4U] = 0x100000U;
	failures += check(q35_vtd_switch_root(&io, 0x300000U, 0x200000U) == -2 &&
		!mock.commit_order, "unexpected active root was replaced");
	if (!failures)
		puts("Q35 VT-d register contract: PASS");
	return failures != 0;
}
