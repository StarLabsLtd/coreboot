/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <console/console.h>
#include <cbmem.h>
#include <device/device.h>
#include <device/pci_def.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <security/tcg/opal_s3_resume.h>
#include <smm_call.h>

#define OPAL_S3_HINT_SIGNATURE  0x3348504f /* "OPH3" */
#define OPAL_S3_HINT_VERSION    0x0001
#define OPAL_S3_HINT_FLAG_RESUME_DEFERRED (1 << 0)

struct opal_s3_hint {
	u32 signature;
	u16 version;
	u16 size;

	u8 bus;
	u8 dev;
	u8 func;
	u8 flags;
} __packed;

#if CONFIG(SOC_INTEL_COMMON_BLOCK_PCIE_RTD3)
#include <soc/intel/common/block/pcie/rtd3/chip.h>

extern struct chip_operations soc_intel_common_block_pcie_rtd3_ops;

static bool opal_s3_platform_has_storage_rtd3(void)
{
	for (DEVTREE_CONST struct device *dev = all_devices; dev; dev = dev->next) {
		if (dev->chip_ops != &soc_intel_common_block_pcie_rtd3_ops)
			continue;

		const struct soc_intel_common_block_pcie_rtd3_config *config = config_of(dev);
		if (config->is_storage)
			return true;
	}

	return false;
}
#else
static bool opal_s3_platform_has_storage_rtd3(void)
{
	return false;
}
#endif

static bool opal_s3_nvme_present(pci_devfn_t nvme_dev)
{
	const u16 vendor = pci_s_read_config16(nvme_dev, PCI_VENDOR_ID);
	const u16 device = pci_s_read_config16(nvme_dev, PCI_DEVICE_ID);

	if (vendor == 0xffff || device == 0xffff)
		return false;
	if (vendor == 0x0000 || device == 0x0000)
		return false;

	return true;
}

void opal_s3_resume_unlock(void)
{
	struct opal_s3_hint *hint = NULL;

	/*
	 * If the NVMe is behind an RTD3 root port it may still be powered off on
	 * resume. In that case, skip the early resume trigger so the later ACPI
	 * _ON-triggered SMI is the only one issued.
	 */
	if (cbmem_online())
		hint = cbmem_find(CBMEM_ID_OPAL_S3_HINT);

	if (hint &&
	    hint->signature == OPAL_S3_HINT_SIGNATURE &&
	    hint->version == OPAL_S3_HINT_VERSION &&
	    hint->size == sizeof(*hint)) {
		const pci_devfn_t nvme_dev = PCI_DEV(hint->bus, hint->dev, hint->func);
		if (!opal_s3_nvme_present(nvme_dev) && opal_s3_platform_has_storage_rtd3()) {
			hint->flags |= OPAL_S3_HINT_FLAG_RESUME_DEFERRED;
			if (CONFIG(DEBUG_SMI)) {
				printk(BIOS_DEBUG, "OPAL-S3: deferring resume trigger for %02x:%02x.%x\n",
				       hint->bus, hint->dev, hint->func);
			}
			return;
		}
	}

	/* Best-effort: trigger OPAL unlock early on S3 resume. */
	u32 rc = call_smm(APM_CNT_OPAL_S3_UNLOCK, 0, NULL);
	if (CONFIG(DEBUG_SMI)) {
		/* Keep logs quiet unless explicitly debugging SMIs. */
		printk(BIOS_DEBUG, "OPAL-S3: resume unlock rc=0x%x\n", rc);
	}
}
