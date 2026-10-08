/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <amdblocks/smu.h>
#include <arch/io.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <soc/pci_devs.h>
#include <soc/smu.h>
#include <starlabs/smi.h>

/* Board-private APMC commands, paired with acpi/usb4.asl. */
#define USB4_NHI0_PS3 0xe5
#define USB4_NHI1_PS3 0xe6
#define SMN_INDEX_ADDR 0xb8
#define USB4_PM_CAP 0x50

static bool usb4_sleep(unsigned int router)
{
	const pci_devfn_t bridge = PCI_DEV(0, 8, 3);
	struct smu_payload payload = { .msg = {router} };
	u32 saved_index;
	u16 command, pmcsr;
	u8 bus;
	bool success = false;

	/* Only the two integrated Phoenix NHIs; no caller-selected PCI address. */
	if (pci_read_config32(bridge, PCI_VENDOR_ID) != 0x14eb1022)
		return false;
	bus = pci_read_config8(bridge, PCI_SECONDARY_BUS);
	if (!bus || bus == 0xff)
		return false;
	const pci_devfn_t nhi = PCI_DEV(bus, 0, 4 + router);
	if (pci_read_config32(nhi, PCI_VENDOR_ID) !=
	    ((0x1667 + router) << 16 | 0x1022) ||
	    pci_read_config8(nhi, USB4_PM_CAP) != PCI_CAP_ID_PM)
		return false;
	pmcsr = pci_read_config16(nhi, USB4_PM_CAP + PCI_PM_CTRL);
	if (pmcsr == 0xffff ||
	    (pmcsr & PCI_PM_CTRL_STATE_MASK) != PCI_PM_CTRL_POWER_STATE_D3HOT)
		return false;

	/* SMI can interrupt an OS SMN index/data access. Restore its index. */
	saved_index = pci_read_config32(SOC_GNB_DEV, SMN_INDEX_ADDR);
	if (send_smu_message_timeout(SMC_MSG_USB4_SLEEP, &payload, 100000) != CB_SUCCESS)
		goto out;

	/*
	 * The OS has saved PCI configuration before _PS3. Retained BME otherwise
	 * makes PCI restore skip the write needed to restart NHI DMA after D0.
	 * Clear only BME; leave memory decoding, PME and interrupt setup alone.
	 */
	command = pci_read_config16(nhi, PCI_COMMAND);
	if (command == 0xffff)
		goto out;
	pci_write_config16(nhi, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
	success = pci_read_config16(nhi, PCI_COMMAND) ==
		(command & ~PCI_COMMAND_MASTER);
out:
	pci_write_config32(SOC_GNB_DEV, SMN_INDEX_ADDR, saved_index);
	return success;
}

int variant_smi_apmc(u8 command)
{
	bool success;

	if (command != USB4_NHI0_PS3 && command != USB4_NHI1_PS3)
		return 0;

	success = usb4_sleep(command - USB4_NHI0_PS3 + 1);
	outb(success ? 0 : 1, APM_STS);
	printk(success ? BIOS_DEBUG : BIOS_ERR, "USB4 NHI%u PS3 handoff %s\n",
	       command - USB4_NHI0_PS3, success ? "complete" : "failed");
	return 1;
}
