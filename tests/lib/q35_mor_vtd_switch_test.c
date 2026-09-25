/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vtd_registers.h"

#define IOTLB_HIGH 0xfcU
#define OLD_ROOT 0x100000U
#define NEW_ROOT 0x200000U

struct mock {
	uint32_t registers[0x1000 / 4];
	unsigned int sequence;
	unsigned int commit_order;
	unsigned int root_low_order;
	unsigned int command_order;
	unsigned int context_order;
	unsigned int iotlb_order;
	unsigned int command_writes;
	uint32_t command;
	bool block_root_write;
};

static uint32_t mock_read(void *context, uint32_t offset)
{
	struct mock *mock = context;

	return mock->registers[offset / 4U];
}

static void mock_write(void *context, uint32_t offset, uint32_t value)
{
	struct mock *mock = context;

	if (offset == Q35_VTD_RTADDR) {
		mock->root_low_order = ++mock->sequence;
		if (mock->block_root_write)
			return;
	} else if (offset == Q35_VTD_GCMD) {
		mock->command_order = ++mock->sequence;
		mock->command_writes++;
		mock->command = value;
		mock->registers[Q35_VTD_GSTS / 4U] = value;
	} else if (offset == Q35_VTD_CCMD + 4U && (value & (1U << 31))) {
		mock->context_order = ++mock->sequence;
		value = 1U << 27;
	} else if (offset == IOTLB_HIGH && (value & (1U << 31))) {
		mock->iotlb_order = ++mock->sequence;
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
	mock->registers[Q35_VTD_ECAP / 4U] = 0xf00f0aU;
	mock->registers[Q35_VTD_GSTS / 4U] =
		Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE;
	mock->registers[Q35_VTD_RTADDR / 4U] = OLD_ROOT;
}

static int check(bool condition, const char *message)
{
	if (condition)
		return 0;
	fprintf(stderr, "Q35 MOR VT-d switch test: %s\n", message);
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
	const uint32_t active =
		Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE;
	int failures = 0;

	setup(&mock);
	failures += check(q35_vtd_switch_root(&io, OLD_ROOT, NEW_ROOT) == 0,
		"protected-root switch failed");
	failures += check(mock.command_writes == 1U && mock.command == active,
		"root switch did not issue TE|SRTP in one command");
	failures += check(mock.commit_order < mock.root_low_order &&
		mock.root_low_order < mock.command_order &&
		mock.command_order < mock.context_order &&
		mock.context_order < mock.iotlb_order,
		"commit/root/adopt/invalidate ordering is wrong");
	failures += check(mock.registers[Q35_VTD_RTADDR / 4U] == NEW_ROOT &&
		(mock.registers[Q35_VTD_GSTS / 4U] & active) == active,
		"translation or replacement root was not retained");

	setup(&mock);
	mock.registers[Q35_VTD_GSTS / 4U] = Q35_VTD_ROOT_SET;
	failures += check(q35_vtd_switch_root(&io, OLD_ROOT, NEW_ROOT) == -2 &&
		!mock.command_writes && !mock.commit_order,
		"switch accepted inactive translation");

	setup(&mock);
	mock.registers[Q35_VTD_RTADDR / 4U] = 0x300000U;
	failures += check(q35_vtd_switch_root(&io, OLD_ROOT, NEW_ROOT) == -2 &&
		!mock.command_writes && !mock.commit_order,
		"switch accepted an unexpected active root");

	setup(&mock);
	mock.block_root_write = true;
	failures += check(q35_vtd_switch_root(&io, OLD_ROOT, NEW_ROOT) == -3 &&
		!mock.command_writes,
		"failed replacement-root readback still issued SRTP");

	setup(&mock);
	failures += check(q35_vtd_switch_root(&io, OLD_ROOT, OLD_ROOT) == -1 &&
		!mock.command_writes && !mock.commit_order,
		"same-root switch was accepted");

	if (!failures)
		puts("Q35 MOR VT-d continuous switch contract: PASS");
	return failures != 0;
}
