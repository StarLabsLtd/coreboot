/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <halt.h>
#include <soc/amd/common/block/psp/psp_def.h>

#define CMD_CONFIG_ID_Z_STATE_ENABLEMENT_STATUS 0x02

static void send_psp_commands(void *unused)
{
	const uint32_t args[4] = { 0 };

	if (psp_command_set_config(CMD_CONFIG_ID_Z_STATE_ENABLEMENT_STATUS, args)
	    != CB_SUCCESS)
		die("PSP Z-state notification failed\n");

}

BOOT_STATE_INIT_ENTRY(BS_POST_DEVICE, BS_ON_EXIT, send_psp_commands, NULL);
