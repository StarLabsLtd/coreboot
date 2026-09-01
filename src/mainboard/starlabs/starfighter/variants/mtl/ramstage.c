/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/intel/gma/opregion.h>
#include <intelblocks/pcie_rp.h>
#include <soc/ramstage.h>
#include <variants.h>
#include <common/fsp_params.h>
#include <common/pin_mux.h>

void mainboard_silicon_init_params(FSP_S_CONFIG *supd)
{
	configure_pin_mux(supd);
	starlabs_update_fsp_s_policy(supd);
	supd->TcNotifyIgd = 2; // Auto

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION))
		return;

	supd->PcieRpAcsEnabled[PCH_RP(1)] = 1;
	supd->PcieRpAcsEnabled[PCH_RP(9)] = 1;
	supd->PcieRpAcsEnabled[PCH_RP(10)] = 1;
	supd->ITbtPcieTunnelingForUsb4 = 1;
}

const char *mainboard_vbt_filename(void)
{
	if (get_memory_config_straps() == 13)
		return "vbt_qhd.bin";
	return "vbt.bin";
}
