/* SPDX-License-Identifier: GPL-2.0-only */

#define __SIMPLE_DEVICE__

#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <intelblocks/cse.h>
#include <intelblocks/p2sb.h>
#include <intelblocks/pcr.h>
#include <intelblocks/pmc_ipc.h>
#include <soc/pci_devs.h>
#include <soc/pcr_ids.h>

#define CSME0_FBE	0xf
#define CSME0_BAR	0x0
#define CSME0_FID	0xb0

#define PMC_IPC_MEI_DISABLE_ID			0xa9
#define PMC_IPC_MEI_DISABLE_SUBID_ENABLE	0
#define PMC_IPC_MEI_DISABLE_SUBID_DISABLE	1

static void record_hidden_mei_devices(void)
{
#if ENV_RAMSTAGE
	if (!CONFIG(PAYLOAD_RESOURCE_HANDOFF))
		return;

	/* The completed platform operation may have hidden several MEI functions. */
	for (unsigned int fn = 0; fn < 8; fn++) {
		struct device *dev = pcidev_path_on_root(PCH_DEVFN_CSE | fn);

		if (dev && pci_read_config16(PCI_DEV(0, PCI_SLOT(PCH_DEVFN_CSE), fn),
					    PCI_VENDOR_ID) == 0xffff)
			dev->hidden = 1;
	}
#endif
}

#if ENV_RAMSTAGE
void cse_finalize_in_smm(void)
{
	if (apm_control(APM_CNT_FINALIZE) == CB_SUCCESS &&
	    CONFIG(PAYLOAD_RESOURCE_HANDOFF) &&
	    CONFIG(SOC_INTEL_COMMON_BLOCK_HECI1_DISABLE_USING_SBI) &&
	    soc_disable_heci1_at_pre_boot())
		record_hidden_mei_devices();
}
#endif

/* Disable HECI using PCR */
static int heci1_disable_using_pcr(void)
{
	int ret = soc_disable_heci1_using_pcr();

	if (!ret)
		record_hidden_mei_devices();
	return ret;
}

bool cse_disable_mei_devices(void)
{
	struct pmc_ipc_buffer req = { 0 };
	struct pmc_ipc_buffer rsp;
	uint32_t cmd;

	cmd = pmc_make_ipc_cmd(PMC_IPC_MEI_DISABLE_ID, PMC_IPC_MEI_DISABLE_SUBID_DISABLE, 0);
	if (pmc_send_ipc_cmd(cmd, &req, &rsp) != CB_SUCCESS) {
		printk(BIOS_ERR, "CSE: Failed to disable MEI devices\n");
		return false;
	}

	record_hidden_mei_devices();
	return true;
}

/* Disable HECI using PMC IPC communication */
static int heci1_disable_using_pmc(void)
{
	return cse_disable_mei_devices() ? 0 : -1;
}

/* Disable HECI using Sideband interface communication */
static int heci1_disable_using_sbi(void)
{
	struct pcr_sbi_msg msg = {
		.pid = PID_CSME0,
		.offset = 0,
		.opcode = PCR_WRITE,
		.is_posted = false,
		.fast_byte_enable = CSME0_FBE,
		.bar = CSME0_BAR,
		.fid = CSME0_FID
	};
	/* Bit 0: Set to make HECI#1 Function disable */
	uint32_t data32 = 1;
	uint8_t response;
	int ret = 0;
	int status;

	/* unhide p2sb device */
	if (!p2sb_unhide())
		die_with_post_code(POSTCODE_HW_INIT_FAILURE,
				   "Unable to change P2SB visibility\n");

	/* Send SBI command to make HECI#1 function disable */
	status = pcr_execute_sideband_msg(PCH_DEV_P2SB, &msg, &data32, &response);
	if (status || response) {
		printk(BIOS_ERR, "Fail to make CSME function disable\n");
		ret = -1;
	}

	/* Ensure to Lock SBI interface after this command */
	if (p2sb_disable_sideband_access())
		ret = -1;

	/* hide p2sb device */
	if (!p2sb_hide())
		die_with_post_code(POSTCODE_HW_INIT_FAILURE,
				   "Unable to change P2SB visibility\n");

	return ret;
}

int heci1_disable(void)
{
	if (ENV_SMM && CONFIG(SOC_INTEL_COMMON_BLOCK_HECI1_DISABLE_USING_SBI)) {
		printk(BIOS_INFO, "Disabling Heci using SBI in SMM mode\n");
		return heci1_disable_using_sbi();
	} else if (!ENV_SMM && CONFIG(SOC_INTEL_COMMON_BLOCK_HECI1_DISABLE_USING_PMC_IPC)) {
		printk(BIOS_INFO, "Disabling Heci using PMC IPC\n");
		return heci1_disable_using_pmc();
	} else if (!ENV_SMM && CONFIG(SOC_INTEL_COMMON_BLOCK_HECI1_DISABLE_USING_PCR)) {
		printk(BIOS_INFO, "Disabling Heci using PCR\n");
		return heci1_disable_using_pcr();
	} else {
		printk(BIOS_ERR, "%s Error: Unable to make HECI1 function disable!\n",
				__func__);
		return -1;
	}
}
