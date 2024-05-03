/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <console/console.h>
#include <soc/platform_descriptors.h>
#include "soc/amd/common/block/psp/psp_def.h"
#include <variants.h>

/* CS Base Buffer structure */
//struct ap_cs_base_buffer {
//	uint32_t value;
//} __packed;

/* MBOX buffer for ApCsBase */
//struct mbox_ap_cs_base_buffer {
//	struct mbox_buffer_header header;
//	struct ap_cs_base_buffer req;
//} __packed;

//static int psp_mbox_bios_send_ap_cs_base(uint32_t ap_cs_base)
//{
//	/* Initialize the buffer for the AP CS Base command */
//	struct mbox_ap_cs_base_buffer buffer = {
//		.header = {
//			.size = sizeof(buffer)
//		},
//		.req = {
//			.value = ap_cs_base
//		}
//	};
//
//	/* Send the command to the PSP */
//	const int cmd_status = send_psp_command(0x1d, &buffer);
//	printk(BIOS_DEBUG, "send_psp_command returned %d\n", cmd_status);
//
//	/* Print the command status and check the response */
//	psp_print_cmd_status(cmd_status, &buffer.header);
//
//	return cmd_status;
//}

void mb_pre_fspm(void)
{
	const struct soc_amd_gpio *pads;
	size_t num;

	pads = variant_gpio_table(&num);
	gpio_configure_pads(pads, num);

//	printk(BIOS_DEBUG, "Setting AP CS Base\n");
//	psp_mbox_bios_send_ap_cs_base(0x7E6C0000);
}
