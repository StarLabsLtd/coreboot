/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootmode.h>
#include <bootstate.h>
#include <console/console.h>
#include <smm_call.h>
#include <smmstore.h>
#include <stdint.h>

static void enable_capsule_smi(void *unused)
{
	uint32_t ret;
	const bool supported = get_boot_mode() == LB_BOOT_MODE_FLASH_UPDATE;

	ret = call_smm(APM_CNT_SMMSTORE, SMMSTORE_CMD_USE_FULL_FLASH,
		       (void *)(uintptr_t)supported);

	printk(BIOS_INFO, "%sabled capsule update SMI handler\n",
	       ret == SMMSTORE_RET_SUCCESS ? "En" : "Dis");
}

BOOT_STATE_INIT_ENTRY(BS_PAYLOAD_BOOT, BS_ON_ENTRY, enable_capsule_smi, NULL);
