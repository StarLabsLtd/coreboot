/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/helpers.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vtd_registers.h"

#define IOTLB_HIGH 0xfcU
#define INVALIDATE (1U << 31)
#define GLOBAL_IOTLB (1U << 28)
#define DRAIN_READS (1U << 17)
#define DRAIN_WRITES (1U << 16)

struct mock {
	uint32_t registers[0x1000U / sizeof(uint32_t)];
	uint32_t drained_request;
	unsigned int sequence;
	unsigned int context_order;
	unsigned int iotlb_order;
	unsigned int drain_order;
	unsigned int writes;
	bool block_context;
	bool block_iotlb;
	bool block_drain;
	bool bad_context_ack;
	bool bad_iotlb_ack;
	bool bad_drain_ack;
	bool change_enabled;
	uint32_t change_register;
};

static void check(bool condition, const char *expression)
{
	if (!condition) {
		fprintf(stderr, "VTD_DRAIN_ASSERT: %s\n", expression);
		abort();
	}
}

#define CHECK(condition) check(!!(condition), #condition)

static uint32_t mock_read(void *context, uint32_t offset)
{
	struct mock *mock = context;

	CHECK(!(offset & 3U) && offset < sizeof(mock->registers));
	return mock->registers[offset / sizeof(uint32_t)];
}

static void mock_write(void *context, uint32_t offset, uint32_t value)
{
	struct mock *mock = context;

	CHECK(!(offset & 3U) && offset < sizeof(mock->registers));
	mock->writes++;
	if (offset == Q35_VTD_CCMD + 4U && (value & INVALIDATE)) {
		CHECK(value == (INVALIDATE | (1U << 29)));
		mock->context_order = ++mock->sequence;
		if (!mock->block_context)
			value = mock->bad_context_ack ? 0U : (1U << 27);
	} else if (offset == IOTLB_HIGH && (value & INVALIDATE)) {
		if (value & (DRAIN_READS | DRAIN_WRITES)) {
			mock->drained_request = value;
			mock->drain_order = ++mock->sequence;
			if (!mock->block_drain)
				value = mock->bad_drain_ack ? 0U : (1U << 25);
			if (mock->change_enabled)
				mock->registers[mock->change_register / sizeof(uint32_t)] ^= 1U;
		} else {
			CHECK(value == (INVALIDATE | GLOBAL_IOTLB));
			mock->iotlb_order = ++mock->sequence;
			if (!mock->block_iotlb)
				value = mock->bad_iotlb_ack ? 0U : (1U << 25);
		}
	}
	mock->registers[offset / sizeof(uint32_t)] = value;
}

static void setup(struct mock *mock)
{
	const uint64_t capability = Q35_VTD_DRAIN_READS_SUPPORTED |
		Q35_VTD_DRAIN_WRITES_SUPPORTED;

	memset(mock, 0, sizeof(*mock));
	mock->registers[Q35_VTD_VERSION / sizeof(uint32_t)] = 0x10U;
	mock->registers[Q35_VTD_CAP / sizeof(uint32_t)] = (uint32_t)capability;
	mock->registers[(Q35_VTD_CAP + 4U) / sizeof(uint32_t)] =
		(uint32_t)(capability >> 32);
	mock->registers[Q35_VTD_ECAP / sizeof(uint32_t)] = 0xf00f0aU;
	mock->registers[Q35_VTD_RTADDR / sizeof(uint32_t)] = 0x100000U;
	mock->registers[Q35_VTD_GSTS / sizeof(uint32_t)] =
		Q35_VTD_ROOT_SET | Q35_VTD_TRANSLATION_ENABLE;
}

int main(void)
{
	struct mock mock;
	struct q35_vtd_io io = {
		.context = &mock,
		.read32 = mock_read,
		.write32 = mock_write,
	};
	const uint32_t changed[] = {
		Q35_VTD_CAP, Q35_VTD_ECAP, Q35_VTD_RTADDR, Q35_VTD_GSTS,
		Q35_VTD_VERSION,
	};

	setup(&mock);
	CHECK(q35_vtd_invalidate_drain(&io) == 0);
	CHECK(mock.drained_request ==
		(INVALIDATE | GLOBAL_IOTLB | DRAIN_READS | DRAIN_WRITES));
	CHECK(mock.context_order < mock.iotlb_order && mock.iotlb_order < mock.drain_order);
	CHECK(q35_vtd_invalidate_drain(NULL) == -1);
	setup(&mock);
	io.read32 = NULL;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	io.read32 = mock_read;
	io.write32 = NULL;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	io.write32 = mock_write;
	for (unsigned int bit = 22U; bit <= 23U; bit++) {
		setup(&mock);
		mock.registers[(Q35_VTD_CAP + 4U) / sizeof(uint32_t)] &= ~(1U << bit);
		CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	}
	setup(&mock);
	mock.registers[Q35_VTD_GSTS / sizeof(uint32_t)] |=
		Q35_VTD_QUEUED_INVALIDATION_ENABLE;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	for (unsigned int bit = 30U; bit <= 31U; bit++) {
		setup(&mock);
		mock.registers[Q35_VTD_GSTS / sizeof(uint32_t)] &= ~(1U << bit);
		CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	}
	setup(&mock);
	mock.registers[(Q35_VTD_CCMD + 4U) / sizeof(uint32_t)] = INVALIDATE;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	setup(&mock);
	mock.registers[IOTLB_HIGH / sizeof(uint32_t)] = INVALIDATE;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	setup(&mock);
	mock.registers[Q35_VTD_RTADDR / sizeof(uint32_t)] = 0;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	setup(&mock);
	mock.registers[Q35_VTD_RTADDR / sizeof(uint32_t)] |= 1U;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	setup(&mock);
	mock.registers[Q35_VTD_VERSION / sizeof(uint32_t)] = UINT32_MAX;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	setup(&mock);
	mock.registers[Q35_VTD_ECAP / sizeof(uint32_t)] = 0;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.writes);
	setup(&mock);
	mock.block_context = true;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.drain_order);
	setup(&mock);
	mock.block_iotlb = true;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.drain_order);
	setup(&mock);
	mock.block_drain = true;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && mock.drain_order);
	setup(&mock);
	mock.bad_context_ack = true;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.drain_order);
	setup(&mock);
	mock.bad_iotlb_ack = true;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && !mock.drain_order);
	setup(&mock);
	mock.bad_drain_ack = true;
	CHECK(q35_vtd_invalidate_drain(&io) == -1 && mock.drain_order);
	for (size_t index = 0; index < ARRAY_SIZE(changed); index++) {
		setup(&mock);
		mock.change_enabled = true;
		mock.change_register = changed[index];
		CHECK(q35_vtd_invalidate_drain(&io) == -1 && mock.drain_order);
	}
	puts("HOST modeled VT-d global drain request/acknowledgment: PASS (not DMA authority)");
	return 0;
}
