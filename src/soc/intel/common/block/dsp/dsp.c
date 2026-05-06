/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <bootstate.h>
#include <device/device.h>
#include <device/pci.h>
#include <device/pci_ids.h>

static void dsp_final(struct device *dev)
{
	if (CONFIG(PAYLOAD_OWNS_PCI_DEVICES) && !acpi_is_wakeup_s3())
		pci_dev_disable_bus_master(dev);
	else
		pci_dev_request_bus_master(dev);
}

static struct device_operations dsp_dev_ops = {
	.read_resources         = pci_dev_read_resources,
	.set_resources          = pci_dev_set_resources,
	.enable_resources       = pci_dev_enable_resources,
	.ops_pci                = &pci_dev_ops_pci,
	.scan_bus               = scan_static_bus,
	.final                  = dsp_final,
};

static const unsigned short pci_device_ids[] = {
	PCI_DID_INTEL_NVL_AUDIO_1,
	PCI_DID_INTEL_NVL_AUDIO_2,
	PCI_DID_INTEL_NVL_AUDIO_3,
	PCI_DID_INTEL_NVL_AUDIO_4,
	PCI_DID_INTEL_NVL_AUDIO_5,
	PCI_DID_INTEL_NVL_AUDIO_6,
	PCI_DID_INTEL_NVL_AUDIO_7,
	PCI_DID_INTEL_NVL_AUDIO_8,
	PCI_DID_INTEL_WCL_AUDIO_1,
	PCI_DID_INTEL_WCL_AUDIO_2,
	PCI_DID_INTEL_WCL_AUDIO_3,
	PCI_DID_INTEL_WCL_AUDIO_4,
	PCI_DID_INTEL_WCL_AUDIO_5,
	PCI_DID_INTEL_WCL_AUDIO_6,
	PCI_DID_INTEL_WCL_AUDIO_7,
	PCI_DID_INTEL_WCL_AUDIO_8,
	PCI_DID_INTEL_PTL_H_AUDIO_1,
	PCI_DID_INTEL_PTL_H_AUDIO_2,
	PCI_DID_INTEL_PTL_H_AUDIO_3,
	PCI_DID_INTEL_PTL_H_AUDIO_4,
	PCI_DID_INTEL_PTL_H_AUDIO_5,
	PCI_DID_INTEL_PTL_H_AUDIO_6,
	PCI_DID_INTEL_PTL_H_AUDIO_7,
	PCI_DID_INTEL_PTL_H_AUDIO_8,
	PCI_DID_INTEL_PTL_U_H_AUDIO_1,
	PCI_DID_INTEL_PTL_U_H_AUDIO_2,
	PCI_DID_INTEL_PTL_U_H_AUDIO_3,
	PCI_DID_INTEL_PTL_U_H_AUDIO_4,
	PCI_DID_INTEL_PTL_U_H_AUDIO_5,
	PCI_DID_INTEL_PTL_U_H_AUDIO_6,
	PCI_DID_INTEL_PTL_U_H_AUDIO_7,
	PCI_DID_INTEL_PTL_U_H_AUDIO_8,
	PCI_DID_INTEL_LNL_AUDIO_1,
	PCI_DID_INTEL_LNL_AUDIO_2,
	PCI_DID_INTEL_LNL_AUDIO_3,
	PCI_DID_INTEL_LNL_AUDIO_4,
	PCI_DID_INTEL_LNL_AUDIO_5,
	PCI_DID_INTEL_LNL_AUDIO_6,
	PCI_DID_INTEL_LNL_AUDIO_7,
	PCI_DID_INTEL_LNL_AUDIO_8,
	PCI_DID_INTEL_MTL_AUDIO_1,
	PCI_DID_INTEL_MTL_AUDIO_2,
	PCI_DID_INTEL_MTL_AUDIO_3,
	PCI_DID_INTEL_MTL_AUDIO_4,
	PCI_DID_INTEL_MTL_AUDIO_5,
	PCI_DID_INTEL_MTL_AUDIO_6,
	PCI_DID_INTEL_MTL_AUDIO_7,
	PCI_DID_INTEL_MTL_AUDIO_8,
	PCI_DID_INTEL_ARL_AUDIO,
	PCI_DID_INTEL_ARP_S_AUDIO,
	PCI_DID_INTEL_RPP_P_AUDIO,
	PCI_DID_INTEL_RPP_S_AUDIO_1,
	PCI_DID_INTEL_RPP_S_AUDIO_2,
	PCI_DID_INTEL_RPP_S_AUDIO_3,
	PCI_DID_INTEL_RPP_S_AUDIO_4,
	PCI_DID_INTEL_RPP_S_AUDIO_5,
	PCI_DID_INTEL_RPP_S_AUDIO_6,
	PCI_DID_INTEL_RPP_S_AUDIO_7,
	PCI_DID_INTEL_RPP_S_AUDIO_8,
	PCI_DID_INTEL_APL_AUDIO,
	PCI_DID_INTEL_CNL_AUDIO,
	PCI_DID_INTEL_GLK_AUDIO,
	PCI_DID_INTEL_SKL_AUDIO,
	PCI_DID_INTEL_CNP_H_AUDIO,
	PCI_DID_INTEL_CMP_AUDIO,
	PCI_DID_INTEL_CMP_H_AUDIO,
	PCI_DID_INTEL_TGL_AUDIO,
	PCI_DID_INTEL_TGL_H_AUDIO,
	PCI_DID_INTEL_MCC_AUDIO,
	PCI_DID_INTEL_JSP_AUDIO,
	PCI_DID_INTEL_ADP_P_AUDIO,
	PCI_DID_INTEL_ADP_S_AUDIO_1,
	PCI_DID_INTEL_ADP_S_AUDIO_2,
	PCI_DID_INTEL_ADP_S_AUDIO_3,
	PCI_DID_INTEL_ADP_S_AUDIO_4,
	PCI_DID_INTEL_ADP_S_AUDIO_5,
	PCI_DID_INTEL_ADP_S_AUDIO_6,
	PCI_DID_INTEL_ADP_S_AUDIO_7,
	PCI_DID_INTEL_ADP_S_AUDIO_8,
	PCI_DID_INTEL_ADP_M_N_AUDIO_1,
	PCI_DID_INTEL_ADP_M_N_AUDIO_2,
	PCI_DID_INTEL_ADP_M_N_AUDIO_3,
	PCI_DID_INTEL_ADP_M_N_AUDIO_4,
	PCI_DID_INTEL_ADP_M_N_AUDIO_5,
	PCI_DID_INTEL_ADP_M_N_AUDIO_6,
	PCI_DID_INTEL_ADP_M_N_AUDIO_7,
	0,
};

static void dsp_disable_bme_before_payload(void *unused)
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
		      dsp_disable_bme_before_payload, NULL);

static const struct pci_driver dsp_driver __pci_driver = {
	.ops    = &dsp_dev_ops,
	.vendor = PCI_VID_INTEL,
	.devices = pci_device_ids,
};
