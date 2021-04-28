/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <console/console.h>
#include <device/pci.h>
#include <device/pci_ids.h>
#include <device/pci_ops.h>
#include <intelblocks/cse.h>
#include <intelblocks/p2sb.h>
#include <intelblocks/pcr.h>
#include <soc/pci_devs.h>
#include <soc/pcr_ids.h>
#include <option.h>
#include <types.h>

void disable_me(void)
{
        u8 me_state = get_int_option("me_state", 0xff);
        printk(BIOS_DEBUG, "CMOS me_state: %d\n", me_state);
	/* It's okay, I've got a tin foil hat */
        if(me_state = 0)
		return;

	/* I would like to take the hat off */
	printk(BIOS_DEBUG, "ME: HECI send disable\n");
	struct disable_command {
		uint32_t hdr;
		uint32_t rule_id;
		uint8_t rule_len;
		uint32_t rule_data;
	} __packed msg;
	msg.hdr       = 0x303;
	msg.rule_id   = 6; /* 3 or 6 */
	msg.rule_len  = 4; /* 0x04 */
	msg.rule_data = 0;
	if (!heci_send(&msg, sizeof(msg), BIOS_HOST_ADDR, HECI_MKHI_ADDR))
		printk(BIOS_ERR, "ME: Error sending DISABLE msg\n");
}
