/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_def.h>
#include <soc/dma_binding.h>
#include <soc/dma_guard.h>
#include <soc/dma_policy.h>
#include <string.h>

static uint16_t vendors[CEZANNE_DMA_PCI_FUNCTIONS];
static uint16_t devices[CEZANNE_DMA_PCI_FUNCTIONS];
static uint32_t classes[CEZANNE_DMA_PCI_FUNCTIONS];
static uint16_t commands[CEZANNE_DMA_PCI_FUNCTIONS];
static uint16_t blocked_write = UINT16_MAX;
static int failures;
static uint16_t prh_bdfs[2];
static size_t prh_count;

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

static uint16_t read_device(void *unused, uint16_t bdf)
{
	(void)unused;
	return devices[bdf];
}

static uint32_t read_class_revision(void *unused, uint16_t bdf)
{
	(void)unused;
	return classes[bdf];
}

static void write_command(void *unused, uint16_t bdf, uint16_t command)
{
	(void)unused;
	if (bdf != blocked_write)
		commands[bdf] = command;
}

static const struct cezanne_dma_pci_io io = {
	.read_vendor = read_vendor,
	.read_device = read_device,
	.read_class_revision = read_class_revision,
	.read_command = read_command,
	.write_command = write_command,
};

static void reset_model(void)
{
	memset(vendors, 0xff, sizeof(vendors));
	memset(devices, 0xff, sizeof(devices));
	memset(classes, 0xff, sizeof(classes));
	memset(commands, 0, sizeof(commands));
	blocked_write = UINT16_MAX;
}

static void check(bool condition)
{
	failures += !condition;
}

static bool prh_contains(void *unused, uint16_t segment, uint16_t bdf)
{
	(void)unused;
	if (segment)
		return false;
	for (size_t index = 0; index < prh_count; index++)
		if (prh_bdfs[index] == bdf)
			return true;
	return false;
}

int main(void)
{
	static const uint8_t first_nvme;
	static const uint8_t second_nvme;
	static const uint8_t unselected_nvme;
	struct cezanne_dma_policy policy = { 0 };
	struct cezanne_dma_pci_snapshot snapshot;
	struct amd_iommu_dma_requester requesters[2] = {
		{ .device_id = 0x100 },
		{ .device_id = 0x201 },
	};
	uintptr_t payload;
	uint32_t pages;
	uint16_t priority;

	check(cezanne_dma_requester_in_aperture(0x3fff, 64U * 256U));
	check(!cezanne_dma_requester_in_aperture(0x4000, 64U * 256U));
	check(!cezanne_dma_requester_in_aperture(0, 0));
	check(cezanne_dma_iommu_resource_valid(0xf0000000, 512U * 1024U,
		0xf0000001, 0, UINT32_MAX));
	check(cezanne_dma_iommu_resource_valid(0xfff80000, 512U * 1024U,
		0xfff80001, 0, UINT32_MAX));
	check(!cezanne_dma_iommu_resource_valid(0xfff80000, 512U * 1024U,
		0xfff80001, 0, UINT32_MAX - 1U));
	check(!cezanne_dma_iommu_resource_valid(0x100000000ULL, 512U * 1024U,
		1, 1, UINT32_MAX));
	check(!cezanne_dma_iommu_resource_valid(0xf0004000, 512U * 1024U,
		0xf0004001, 0, UINT32_MAX));
	check(!cezanne_dma_iommu_resource_valid(0xf0000000, 512U * 1024U,
		0xf0000003, 0, UINT32_MAX));
	check(!cezanne_dma_iommu_resource_valid(0xf0000000, 256U * 1024U,
		0xf0000001, 0, UINT32_MAX));
	check(!cezanne_dma_iommu_resource_valid(0xf0000000, 512U * 1024U,
		0xf0000000, 0, UINT32_MAX));
	check(!cezanne_dma_iommu_resource_valid(0xf0000000, 512U * 1024U,
		0xf0000001, 1, UINT64_MAX));
	check(!cezanne_dma_iommu_resource_valid(0, 512U * 1024U, 1, 0,
		UINT32_MAX));
	check(cezanne_dma_cbmem_layout(0x1003, 0x2fff, 0x2000, 0x1000,
		&payload) && payload == 0x2000);
	check(!cezanne_dma_cbmem_layout(0, 0x2fff, 0x2000, 0x1000, &payload));
	check(!cezanne_dma_cbmem_layout(0x1003, 0x3000, 0x2000, 0x1000,
		&payload));
	check(!cezanne_dma_cbmem_layout(UINTPTR_MAX - 0x1000, 0x2fff,
		0x2000, 0x1000, &payload));
	check(!cezanne_dma_cbmem_layout(0x1000, 0x2fff, 0x2000, 0x1800,
		&payload));
	prh_bdfs[0] = requesters[0].device_id;
	prh_bdfs[1] = requesters[1].device_id;
	prh_count = 2;
	check(cezanne_dma_prh_matches(1, true, 2, requesters, 2,
		prh_contains, NULL));
	check(!cezanne_dma_prh_matches(0, true, 2, requesters, 2,
		prh_contains, NULL));
	check(!cezanne_dma_prh_matches(1, false, 2, requesters, 2,
		prh_contains, NULL));
	check(!cezanne_dma_prh_matches(1, true, 1, requesters, 2,
		prh_contains, NULL));
	prh_count = 1;
	check(!cezanne_dma_prh_matches(1, true, 2, requesters, 2,
		prh_contains, NULL));

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
	devices[0x0100] = 0x1639;
	devices[0x0201] = 0xa80a;
	classes[0x0100] = 0x0c033001;
	classes[0x0201] = 0x01080200;
	commands[0x0100] = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	commands[0x0201] = PCI_COMMAND_MASTER;
	check(cezanne_dma_pci_quiesce(&io, &snapshot, CEZANNE_DMA_PCI_FUNCTIONS));
	check(!(commands[0x0100] & PCI_COMMAND_MASTER) &&
		!(commands[0x0201] & PCI_COMMAND_MASTER));
	check(cezanne_dma_pci_quiescence_held(&io, &snapshot));

	commands[0x0201] |= PCI_COMMAND_MASTER;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));
	commands[0x0201] &= ~PCI_COMMAND_MASTER;
	devices[0x0201]++;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));
	devices[0x0201]--;
	classes[0x0201]++;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));
	classes[0x0201]--;
	vendors[0x0300] = 0x1b21;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));
	vendors[0x0300] = UINT16_MAX;
	vendors[0x0201] = UINT16_MAX;
	check(!cezanne_dma_pci_quiescence_held(&io, &snapshot));

	reset_model();
	vendors[0x0400] = 0x1022;
	devices[0x0400] = 0x1639;
	classes[0x0400] = 0x06000001;
	commands[0x0400] = PCI_COMMAND_MASTER;
	blocked_write = 0x0400;
	check(!cezanne_dma_pci_quiesce(&io, &snapshot, CEZANNE_DMA_PCI_FUNCTIONS));
	check(!snapshot.valid);

	check(!cezanne_dma_pci_quiesce(NULL, &snapshot, CEZANNE_DMA_PCI_FUNCTIONS));
	check(!cezanne_dma_pci_quiesce(&io, &snapshot, 0));
	check(!cezanne_dma_pci_quiesce(&io, &snapshot,
		CEZANNE_DMA_PCI_FUNCTIONS + 1U));
	check(!cezanne_dma_pci_quiescence_held(&io, NULL));

	reset_model();
	for (size_t index = 0; index <= CEZANNE_DMA_PCI_SNAPSHOT_MAX; index++) {
		vendors[index] = 0x1022;
		devices[index] = index;
		classes[index] = 0x06000000;
	}
	check(!cezanne_dma_pci_quiesce(&io, &snapshot,
		CEZANNE_DMA_PCI_SNAPSHOT_MAX + 1U));
	check(!snapshot.valid);

	if (failures)
		return 1;
	return 0;
}
