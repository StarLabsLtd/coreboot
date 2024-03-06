/* SPDX-License-Identifier: GPL-2.0-only */

#include <option.h>
#include <console/console.h>
#include <soc/ramstage.h>

void mainboard_silicon_init_params(FSP_S_CONFIG *supd)
{
	supd->CnviRfResetPinMux = 0x194ce404;
	supd->CnviClkreqPinMux = 0x294ce605;
	printk(BIOS_INFO, "CNVi: Set\n");
	printk(BIOS_INFO, "CnviRfResetPinMux: 0x%x\n", supd->CnviRfResetPinMux);
	printk(BIOS_INFO, "CnviClkreqPinMux: 0x%x\n", supd->CnviClkreqPinMux);
}
