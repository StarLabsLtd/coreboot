/* SPDX-License-Identifier: GPL-2.0-only */

#include <common/fsp_params.h>
#include <common/pin_mux.h>
#include <intelblocks/pcie_rp.h>
#include <soc/ramstage.h>

void mainboard_silicon_init_params(FSP_S_CONFIG *supd)
{
	configure_pin_mux(supd);
	starlabs_update_fsp_s_policy(supd);
	supd->TcNotifyIgd = 2; // Auto

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION))
		return;

	supd->PcieRpAcsEnabled[PCH_RP(9)] = 1;
	supd->PcieRpAcsEnabled[PCH_RP(10)] = 1;

	supd->ITbtPcieTunnelingForUsb4 = 1;
	supd->ITbtPcieRootPortEn[0] = 1;
	supd->ITbtPcieRootPortEn[1] = 1;
}
