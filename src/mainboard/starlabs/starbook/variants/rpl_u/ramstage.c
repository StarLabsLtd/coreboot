/* SPDX-License-Identifier: GPL-2.0-only */

#include <common/fsp_params.h>
#include <common/pin_mux.h>
#include <intelblocks/pcie_rp.h>
#include <option.h>
#include <soc/ramstage.h>

void mainboard_silicon_init_params(FSP_S_CONFIG *supd)
{
	starlabs_update_fsp_s_policy(supd);
	configure_pin_mux(supd);

	if (CONFIG(ENABLE_EARLY_DMA_PROTECTION)) {
		supd->CpuPcieRpAcsEnabled[CPU_RP(1)] = 1;
		supd->ITbtPcieTunnelingForUsb4 = 1;
		return;
	}

	if (get_uint_option("thunderbolt", 1) == 0)
		supd->UsbTcPortEn = 0;
}
