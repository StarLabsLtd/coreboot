/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_def.h>
#include <soc/dma_guard.h>
#include <soc/dma_policy.h>
#include <string.h>

static uint16_t vendors[CEZANNE_DMA_PCI_FUNCTIONS];
static uint16_t commands[CEZANNE_DMA_PCI_FUNCTIONS];
static uint16_t blocked_write = UINT16_MAX;
static int failures;

static uint16_t read_vendor(void *unused, uint16_t bdf)
{
	(void)unused;
	return vendors[bdf];
}

static uint16_t read_command(void *unused, uint16_t bdf)
{
	(void)unused;
	return commands[bdf];
}

static void write_command(void *unused, uint16_t bdf, uint16_t command)
{
	(void)unused;
	if (bdf != blocked_write)
		commands[bdf] = command;
}

static const struct cezanne_dma_pci_io io = {
	.read_vendor = read_vendor,
	.read_command = read_command,
	.write_command = write_command,
};

static void reset_model(void)
{
	memset(vendors, 0xff, sizeof(vendors));
	memset(commands, 0, sizeof(commands));
	blocked_write = UINT16_MAX;
}

static void check(bool condition)
{
	failures += !condition;
}

int main(void)
{
	static const uint8_t first_nvme;
	static const uint8_t second_nvme;
	static const uint8_t unselected_nvme;
	struct cezanne_dma_policy policy = { 0 };
	struct cezanne_dma_pci_snapshot snapshot;
	uint32_t pages;
	uint16_t priority;

	check(cezanne_dma_policy_add(&policy, &first_nvme, 0x010802,
		CEZANNE_DMA_BOOT_NVME, 10, &pages) == CB_SUCCESS && pages == 32);
	check(cezanne_dma_policy_add(&policy, &second_nvme, 0x010802,
		CEZANNE_DMA_BOOT_NVME, 20, &pages) == CB_SUCCESS && pages == 32);
	check(cezanne_dma_policy_add(&policy, &first_nvme, 0x010802,
		CEZANNE_DMA_BOOT_NVME, 30, NULL) == CB_ERR_ARG);
	check(cezanne_dma_policy_add(&policy, &unselected_nvme, 0x010802,
		CEZANNE_DMA_BOOT_NVME, 20, NULL) == CB_ERR_ARG);
	check(cezanne_dma_policy_add(&policy, &unselected_nvme, 0x010802,
		CEZANNE_DMA_BOOT_XHCI, 30, NULL) == CB_ERR_ARG);
	check(cezanne_dma_policy_freeze(&policy));
	check(cezanne_dma_policy_lookup(&policy, &first_nvme, &priority) &&
		priority == 10);
	check(cezanne_dma_policy_lookup(&policy, &second_nvme, &priority) &&
		priority == 20);
	check(!cezanne_dma_policy_lookup(&policy, &unselected_nvme, &priority));
	check(cezanne_dma_policy_add(&policy, &unselected_nvme, 0x010802,
		CEZANNE_DMA_BOOT_NVME, 30, NULL) == CB_ERR_ARG);

	reset_model();
	vendors[0x0100] = 0x1022;
	vendors[0x0201] = 0x144d;
	commands[0x0100] = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	commands[0x0201] = PCI_COMMAND_MASTER;
	check(cezanne_dma_pci_quiesce(&io, &snapshot, CEZANNE_DMA_PCI_FUNCTIONS));
	check(!(commands[0x0100] & PCI_COMMAND_MASTER) &&
		!(commands[0x0201] & PCI_COMMAND_MASTER));
	check(cezanne_dma_pci_quiescence_held(&io, &snapshot));

	commands[0x0201] |= PCI_COMMAND_MASTER;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));
	commands[0x0201] &= ~PCI_COMMAND_MASTER;
	vendors[0x0300] = 0x1b21;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));
	vendors[0x0300] = UINT16_MAX;
	vendors[0x0201] = UINT16_MAX;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));

	reset_model();
	vendors[0x0400] = 0x1022;
	commands[0x0400] = PCI_COMMAND_MASTER;
	blocked_write = 0x0400;
	check(!cezanne_dma_pci_quiesce(&io, &snapshot, CEZANNE_DMA_PCI_FUNCTIONS));
	check(!snapshot.valid);

	check(!cezanne_dma_pci_quiesce(NULL, &snapshot, CEZANNE_DMA_PCI_FUNCTIONS));
	check(!cezanne_dma_pci_quiesce(&io, &snapshot, 0));
	check(!cezanne_dma_pci_quiesce(&io, &snapshot,
		CEZANNE_DMA_PCI_FUNCTIONS + 1U));
	check(!cezanne_dma_pci_quiescence_held(&io, NULL));

	if (failures)
		return 1;
	return 0;
}
