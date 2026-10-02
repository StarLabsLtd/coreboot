/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q35_dma_controller_cancel.h"

struct controller {
	uint32_t capability;
	uint32_t ports;
	uint32_t version;
	uint32_t command;
	uint32_t engines[32];
	uint32_t active[32];
	uint32_t issue[32];
	uint32_t pending;
	uint32_t resets;
	uint32_t waited;
	unsigned int residual;
	bool stuck;
	bool changed;
};

static void check(bool value, const char *expression)
{
	if (!value) {
		fprintf(stderr, "AHCI_CANCEL_ASSERT: %s\n", expression);
		abort();
	}
}

#define CHECK(expression) check(!!(expression), #expression)

static uint32_t read_register(void *context, uint32_t offset)
{
	struct controller *device = context;
	const uint32_t port = offset >= 0x100U ? (offset - 0x100U) / 0x80U : 32U;

	switch (offset) {
	case 0x00: return device->capability + (device->changed && device->resets ? 1U : 0U);
	case 0x04: return device->command;
	case 0x0c: return device->ports;
	case 0x10: return device->version;
	default:
		if (port >= 32U)
			return UINT32_MAX;
		switch ((offset - 0x100U) % 0x80U) {
		case 0x18: return device->engines[port];
		case 0x34: return device->active[port];
		case 0x38: return device->issue[port];
		default: return UINT32_MAX;
		}
	}
}

static void write_register(void *context, uint32_t offset, uint32_t value)
{
	struct controller *device = context;

	CHECK(offset == 0x04U);
	device->command = value;
	if (!(value & 1U))
		return;
	device->resets++;
	if (device->stuck)
		return;
	device->command = 0x80000000U;
	device->pending = 0;
	memset(device->engines, 0, sizeof(device->engines));
	memset(device->active, 0, sizeof(device->active));
	memset(device->issue, 0, sizeof(device->issue));
	if (device->residual == 1U)
		device->engines[1] = 1U << 15;
	else if (device->residual == 2U)
		device->active[1] = 1U;
	else if (device->residual == 3U)
		device->issue[1] = 1U;
}

static void delay(void *context, uint32_t microseconds)
{
	struct controller *device = context;

	device->waited += microseconds;
}

static struct controller running(void)
{
	struct controller device = {
		.capability = 1U, .ports = 3U, .version = 0x00010000U,
		.command = 0x80000002U, .pending = 2U,
	};

	device.engines[0] = device.engines[1] = (1U << 15) | (1U << 14) | 0x11U;
	device.active[0] = device.active[1] = 1U;
	device.issue[0] = device.issue[1] = 1U;
	return device;
}

int main(void)
{
	struct controller device = running();
	struct q35_dma_controller_io io = {
		.context = &device, .read32 = read_register,
		.write32 = write_register, .delay_us = delay,
	};

	CHECK(!q35_dma_ahci_cancel(NULL));
	CHECK(q35_dma_ahci_cancel(&io));
	CHECK(device.resets == 1U && device.pending == 0);
	CHECK(q35_dma_ahci_cancel(&io));
	CHECK(device.resets == 2U);
	device = running();
	device.capability = 31U;
	device.ports = UINT32_MAX;
	CHECK(q35_dma_ahci_cancel(&io));
	device = running();
	device.stuck = true;
	CHECK(!q35_dma_ahci_cancel(&io));
	CHECK(device.waited == 1000000U);
	for (unsigned int residual = 1U; residual <= 3U; residual++) {
		device = running();
		device.residual = residual;
		CHECK(!q35_dma_ahci_cancel(&io));
	}
	device = running();
	device.changed = true;
	CHECK(!q35_dma_ahci_cancel(&io));
	device = running();
	device.ports = 4U;
	CHECK(!q35_dma_ahci_cancel(&io));
	CHECK(!device.resets);
	device = running();
	device.capability = UINT32_MAX;
	CHECK(!q35_dma_ahci_cancel(&io));
	device = running();
	device.version = 0x00020000U;
	CHECK(!q35_dma_ahci_cancel(&io));
	device = running();
	device.command = UINT32_MAX;
	CHECK(!q35_dma_ahci_cancel(&io));
	io.delay_us = NULL;
	CHECK(!q35_dma_ahci_cancel(&io));
	puts("PASS HOST AHCI reset boundary model (not DMA authority)");
	return 0;
}
