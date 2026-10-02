/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <amdblocks/smu.h>
#include <bootstate.h>
#include <cbmem.h>
#include <console/console.h>
#include <soc/smu.h>
#include <stdint.h>
#include <string.h>

#define SMU_TOOLS_SIZE		(16 * KiB)

static void provision_smu_tools(void *unused)
{
	struct smu_payload payload = {0};
	void *buffer;

	if (acpi_is_wakeup_s3())
		return;

	/* Match AMI's four reserved pages; PMFW owns this buffer across OS use. */
	buffer = cbmem_add(CBMEM_ID_AMD_SMU_TOOLS, SMU_TOOLS_SIZE);
	if (!buffer || ((uintptr_t)buffer & (4 * KiB - 1)) ||
	    (uintptr_t)buffer > UINT32_MAX - (SMU_TOOLS_SIZE - 1))
		die("Unable to reserve SMU tools buffer below 4 GiB\n");
	memset(buffer, 0, SMU_TOOLS_SIZE);

	payload.msg[0] = (uintptr_t)buffer;
	if (send_smu_message_timeout(SMC_MSG_SET_TOOLS_DRAM_ADDR, &payload, 100000) !=
	    CB_SUCCESS)
		die("SMU tools buffer registration failed\n");

	printk(BIOS_INFO, "SMU tools buffer: %p, size %#x\n", buffer, SMU_TOOLS_SIZE);
}

/* Before ACPI/OS memory maps are written; after silicon/device initialization. */
BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, provision_smu_tools, NULL);
