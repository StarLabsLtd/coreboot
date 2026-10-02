/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q35_dma_controller_cancel.h"

struct controller {
	bool xhci;
	bool stuck;
	bool fatal;
	bool changed;
	bool not_ready;
	uint32_t command;
	uint32_t status;
	uint32_t resets;
	uint32_t pending;
	uint32_t waited;
};

static void check(bool value, const char *expression)
{
	if (!value) {
		fprintf(stderr, "DMA_CANCEL_ASSERT: %s\n", expression);
		abort();
	}
}

#define CHECK(expression) check(!!(expression), #expression)

static uint32_t read_register(void *context, uint32_t offset)
{
	struct controller *device = context;

	if (!device->xhci) {
		switch (offset) {
		case 0x00:
			return 0x0001003fU + (device->changed && device->resets ? 1U : 0U);
		case 0x04: return 0x20U;
		case 0x08: return 0x00010400U;
		case 0x14: return device->command;
		case 0x1c:
			return device->status | (device->fatal ? 2U : 0U);
		default: return UINT32_MAX;
		}
	}
	switch (offset) {
	case 0x00:
		return 0x01000040U + (device->changed && device->resets ? 4U : 0U);
	case 0x40: return device->command;
	case 0x44:
		return device->status | (device->not_ready ? 1U << 11 : 0U);
	default: return UINT32_MAX;
	}
}

static void write_register(void *context, uint32_t offset, uint32_t value)
{
	struct controller *device = context;

	if (!device->xhci && offset == 0x14) {
		if ((device->command & 1U) && !(value & 1U)) {
			device->resets++;
			if (!device->stuck) {
				device->status = 0;
				device->pending = 0;
			}
		}
		device->command = value;
	} else if (device->xhci && offset == 0x40) {
		device->command = value;
		if (!(value & 1U))
			device->status |= 1U;
		if (value & 2U) {
			device->resets++;
			if (!device->stuck) {
				device->command = 0;
				device->status = 1U;
				device->not_ready = false;
				device->pending = 0;
			}
		}
	}
}

static void delay(void *context, uint32_t microseconds)
{
	struct controller *device = context;

	device->waited += microseconds;
}

int main(void)
{
	struct controller device = { .command = 1U, .status = 1U, .pending = 1U };
	struct q35_dma_controller_io io = {
		.context = &device, .read32 = read_register,
		.write32 = write_register, .delay_us = delay,
	};

	CHECK(!q35_dma_nvme_cancel(NULL));
	CHECK(!q35_dma_xhci_cancel(NULL));
	CHECK(q35_dma_nvme_cancel(&io));
	CHECK(device.resets == 1U && device.pending == 0);
	CHECK(q35_dma_nvme_cancel(&io));
	CHECK(device.resets == 1U);
	device = (struct controller) { .command = 1U, .status = 1U, .stuck = true };
	CHECK(!q35_dma_nvme_cancel(&io));
	CHECK(device.waited == 1000000U);
	device = (struct controller) { .command = 1U, .status = 1U, .fatal = true };
	CHECK(!q35_dma_nvme_cancel(&io));
	device = (struct controller) { .command = 1U, .status = 1U, .changed = true };
	CHECK(!q35_dma_nvme_cancel(&io));
	device = (struct controller) { .xhci = true, .command = 1U, .pending = 1U };
	CHECK(q35_dma_xhci_cancel(&io));
	CHECK(device.resets == 1U && device.pending == 0);
	CHECK(device.waited >= 1000U);
	CHECK(q35_dma_xhci_cancel(&io));
	CHECK(device.resets == 2U);
	device = (struct controller) { .xhci = true, .command = 1U, .stuck = true };
	CHECK(!q35_dma_xhci_cancel(&io));
	device = (struct controller) { .xhci = true, .command = 1U, .not_ready = true };
	CHECK(!q35_dma_xhci_cancel(&io));
	CHECK(device.resets == 0);
	device = (struct controller) { .xhci = true, .command = 1U, .changed = true };
	CHECK(!q35_dma_xhci_cancel(&io));
	io.delay_us = NULL;
	CHECK(!q35_dma_nvme_cancel(&io));
	CHECK(!q35_dma_xhci_cancel(&io));
	puts("PASS HOST modeled cold-controller reset boundary (not DMA authority)");
	return 0;
}
