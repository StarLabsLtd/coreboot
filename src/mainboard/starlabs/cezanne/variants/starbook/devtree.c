/* SPDX-License-Identifier: GPL-2.0-only */

#include <option.h>
#include "soc/amd/cezanne/chip.h"
#include <types.h>
#include <variants.h>

void devtree_update(void)
{
	/* Update Power Profile based on CMOS settings */
	// switch (get_power_profile(PP_POWER_SAVER)) {
	// case PP_POWER_SAVER:
	//	cfg->pspp_policy		= DXIO_PSPP_POWERSAVE;
	//	break;
	// case PP_BALANCED:
	//	cfg->pspp_policy		= DXIO_PSPP_BALANCED;
	//	break;
	// case PP_PERFORMANCE:
	//	cfg->pspp_policy		= DXIO_PSPP_PERFORMANCE;
	//	break;
	// }

	/* Enable/Disable Wireless based on CMOS settings */
	// TODO: gpp_bridge_4
	// if (get_uint_option("wireless", 1) == 0)

	/* Enable/Disable Webcam based on CMOS settings */
	// TODO:
	// cfg->usb2_ports[CONFIG_CCD_PORT].enable = get_uint_option("webcam", 1);
}
