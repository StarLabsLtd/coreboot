/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <bootstate.h>
#include <device/azalia_device.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <intelblocks/hda.h>

/* Mainboard overrides. */

__weak bool mainboard_is_hda_codec_enabled(void)
{
	return true;
}

static void hda_init(struct device *dev)
{
	if (CONFIG(SOC_INTEL_COMMON_BLOCK_HDA_VERB) && mainboard_is_hda_codec_enabled())
		azalia_audio_init(dev);
}

static void hda_final(struct device *dev)
{
	if (CONFIG(PAYLOAD_OWNS_PCI_DEVICES) && !acpi_is_wakeup_s3())
		pci_dev_disable_bus_master(dev);
	else
		pci_dev_request_bus_master(dev);
}

struct device_operations hda_ops = {
	.read_resources		= pci_dev_read_resources,
	.set_resources		= pci_dev_set_resources,
	.enable_resources	= pci_dev_enable_resources,
	.init			= hda_init,
	.final			= hda_final,
	.ops_pci		= &pci_dev_ops_pci,
	.scan_bus		= scan_static_bus
};

static const unsigned short pci_device_ids[] = {
	PCI_DID_INTEL_LNL_AUDIO_1,
	PCI_DID_INTEL_LNL_AUDIO_2,
	PCI_DID_INTEL_LNL_AUDIO_3,
	PCI_DID_INTEL_LNL_AUDIO_4,
	PCI_DID_INTEL_LNL_AUDIO_5,
	PCI_DID_INTEL_LNL_AUDIO_6,
	PCI_DID_INTEL_LNL_AUDIO_7,
	PCI_DID_INTEL_LNL_AUDIO_8,
	PCI_DID_INTEL_LWB_AUDIO,
	PCI_DID_INTEL_LWB_AUDIO_SUPER,
	PCI_DID_INTEL_BSW_AUDIO,
	PCI_DID_INTEL_MCC_AUDIO,
	0
};

static void hda_disable_bme_before_payload(void *unused)
{
	(void)unused;

	if (!CONFIG(PAYLOAD_OWNS_PCI_DEVICES))
		return;

	for (const unsigned short *id = pci_device_ids; *id; id++) {
		struct device *dev = NULL;

		while ((dev = dev_find_device(PCI_VID_INTEL, *id, dev)))
			if (is_enabled_pci(dev))
				pci_dev_disable_bus_master(dev);
	}
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY,
		      hda_disable_bme_before_payload, NULL);

static const struct pci_driver pch_hda __pci_driver = {
	.ops		= &hda_ops,
	.vendor		= PCI_VID_INTEL,
	.devices	= pci_device_ids,
};
