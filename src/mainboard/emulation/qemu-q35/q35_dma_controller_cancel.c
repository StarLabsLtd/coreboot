/* SPDX-License-Identifier: GPL-2.0-only */

#include "q35_dma_controller_cancel.h"

#define CANCEL_POLLS 100000U
#define CANCEL_POLL_US 10U
#define NVME_CAP 0x00U
#define NVME_VERSION 0x08U
#define NVME_CC 0x14U
#define NVME_CSTS 0x1cU
#define NVME_CC_ENABLE 1U
#define NVME_CSTS_READY 1U
#define NVME_CSTS_FATAL 2U
#define XHCI_USBCMD 0x00U
#define XHCI_USBSTS 0x04U
#define XHCI_RUN 1U
#define XHCI_RESET 2U
#define XHCI_HALTED 1U
#define XHCI_NOT_READY (1U << 11)
#define AHCI_CAP 0x00U
#define AHCI_GHC 0x04U
#define AHCI_PORTS 0x0cU
#define AHCI_VERSION 0x10U
#define AHCI_RESET 1U
#define AHCI_INTERRUPT_ENABLE 2U
#define AHCI_PORT_BASE 0x100U
#define AHCI_PORT_STRIDE 0x80U
#define AHCI_PORT_COMMAND 0x18U
#define AHCI_PORT_ACTIVE 0x34U
#define AHCI_PORT_ISSUE 0x38U
#define AHCI_PORT_ENGINES ((1U << 0) | (1U << 4) | (1U << 14) | (1U << 15))

static bool io_valid(const struct q35_dma_controller_io *io)
{
	return io && io->read32 && io->write32 && io->delay_us;
}

static bool wait_mask(const struct q35_dma_controller_io *io,
	uint32_t offset, uint32_t mask, uint32_t expected)
{
	for (uint32_t retry = 0; retry < CANCEL_POLLS; retry++) {
		const uint32_t value = io->read32(io->context, offset);

		if (value == UINT32_MAX)
			return false;
		if ((value & mask) == expected)
			return true;
		io->delay_us(io->context, CANCEL_POLL_US);
	}
	return false;
}

bool q35_dma_nvme_cancel(const struct q35_dma_controller_io *io)
{
	uint32_t capability_low, capability_high, version, configuration, status;

	if (!io_valid(io))
		return false;
	capability_low = io->read32(io->context, NVME_CAP);
	capability_high = io->read32(io->context, NVME_CAP + 4U);
	version = io->read32(io->context, NVME_VERSION);
	configuration = io->read32(io->context, NVME_CC);
	if ((!capability_low && !capability_high) || capability_low == UINT32_MAX ||
	    capability_high == UINT32_MAX || version == UINT32_MAX ||
	    (version >> 16) < 1U || (version >> 16) > 2U ||
	    configuration == UINT32_MAX)
		return false;
	/* An actual EN falling edge cancels/drains the controller's queues. */
	io->write32(io->context, NVME_CC, configuration & ~NVME_CC_ENABLE);
	if (!wait_mask(io, NVME_CSTS, NVME_CSTS_READY, 0))
		return false;
	configuration = io->read32(io->context, NVME_CC);
	status = io->read32(io->context, NVME_CSTS);
	return configuration != UINT32_MAX && !(configuration & NVME_CC_ENABLE) &&
		status != UINT32_MAX && !(status & (NVME_CSTS_READY | NVME_CSTS_FATAL)) &&
		io->read32(io->context, NVME_CAP) == capability_low &&
		io->read32(io->context, NVME_CAP + 4U) == capability_high &&
		io->read32(io->context, NVME_VERSION) == version;
}

bool q35_dma_xhci_cancel(const struct q35_dma_controller_io *io)
{
	uint32_t capability, operational, command, status;

	if (!io_valid(io))
		return false;
	capability = io->read32(io->context, 0);
	operational = capability & 0xffU;
	if (operational < 0x20U || (operational & 3U) ||
	    (capability >> 16) < 0x100U || (capability >> 16) > 0x120U ||
	    !wait_mask(io, operational + XHCI_USBSTS, XHCI_NOT_READY, 0))
		return false;
	command = io->read32(io->context, operational + XHCI_USBCMD);
	if (command == UINT32_MAX || (command & XHCI_RESET))
		return false;
	io->write32(io->context, operational + XHCI_USBCMD, command & ~XHCI_RUN);
	if (!wait_mask(io, operational + XHCI_USBSTS, XHCI_HALTED, XHCI_HALTED))
		return false;
	command = io->read32(io->context, operational + XHCI_USBCMD);
	if (command == UINT32_MAX || (command & (XHCI_RUN | XHCI_RESET)))
		return false;
	/* HCH alone does not cancel transfers: always issue the real HC reset. */
	io->write32(io->context, operational + XHCI_USBCMD, command | XHCI_RESET);
	io->delay_us(io->context, 1000U);
	if (!wait_mask(io, operational + XHCI_USBCMD, XHCI_RESET, 0) ||
	    !wait_mask(io, operational + XHCI_USBSTS, XHCI_NOT_READY, 0))
		return false;
	command = io->read32(io->context, operational + XHCI_USBCMD);
	status = io->read32(io->context, operational + XHCI_USBSTS);
	return command != UINT32_MAX && !(command & (XHCI_RUN | XHCI_RESET)) &&
		status != UINT32_MAX && (status & (XHCI_HALTED | XHCI_NOT_READY)) ==
			XHCI_HALTED && io->read32(io->context, 0) == capability;
}

bool q35_dma_ahci_cancel(const struct q35_dma_controller_io *io)
{
	uint32_t capability, ports, version, command, legal_ports;

	if (!io_valid(io))
		return false;
	capability = io->read32(io->context, AHCI_CAP);
	ports = io->read32(io->context, AHCI_PORTS);
	version = io->read32(io->context, AHCI_VERSION);
	command = io->read32(io->context, AHCI_GHC);
	legal_ports = (capability & 31U) == 31U ? UINT32_MAX :
		(1U << ((capability & 31U) + 1U)) - 1U;
	if (capability == UINT32_MAX || !ports || (ports & ~legal_ports) ||
	    (version >> 16) != 1U || (version & 0xffffU) > 0x0301U ||
	    command == UINT32_MAX || (command & AHCI_RESET))
		return false;
	/* Port command/BME clearing alone does not retire already mapped IO. */
	io->write32(io->context, AHCI_GHC,
		(command & ~AHCI_INTERRUPT_ENABLE) | AHCI_RESET);
	if (!wait_mask(io, AHCI_GHC, AHCI_RESET, 0))
		return false;
	for (uint32_t port = 0; port < 32U; port++) {
		const uint32_t base = AHCI_PORT_BASE + port * AHCI_PORT_STRIDE;
		uint32_t engines;

		if (!(ports & (1U << port)))
			continue;
		engines = io->read32(io->context, base + AHCI_PORT_COMMAND);
		if (engines == UINT32_MAX || (engines & AHCI_PORT_ENGINES) ||
		    io->read32(io->context, base + AHCI_PORT_ACTIVE) ||
		    io->read32(io->context, base + AHCI_PORT_ISSUE))
			return false;
	}
	command = io->read32(io->context, AHCI_GHC);
	return command != UINT32_MAX && !(command & (AHCI_RESET | AHCI_INTERRUPT_ENABLE)) &&
		io->read32(io->context, AHCI_CAP) == capability &&
		io->read32(io->context, AHCI_PORTS) == ports &&
		io->read32(io->context, AHCI_VERSION) == version;
}
