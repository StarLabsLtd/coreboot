/* SPDX-License-Identifier: GPL-2.0-only */

#include <fsp/api.h>
#include <option.h>
#include <static.h>
#include <types.h>
#include <variants.h>
#include <soc/platform_descriptors.h>

#include "soc/amd/cezanne/chip.h"

void devtree_update(void)
{
	/* Update Power Profile based on CMOS settings */
	// switch (get_power_profile(PP_POWER_SAVER)) {
	// case PP_POWER_SAVER:
	//	mcfg->pspp_policy		= DXIO_PSPP_POWERSAVE;
	//	break;
	// case PP_BALANCED:
	//	mcfg->pspp_policy		= DXIO_PSPP_BALANCED;
	//	break;
	// case PP_PERFORMANCE:
	//	mcfg->pspp_policy		= DXIO_PSPP_PERFORMANCE;
	//	break;
	// }

	/* Enable/Disable Bluetooth based on CMOS settings */
	// if (get_uint_option("wireless", 1) == 0)
	//	mcfg->? = 0;

	/* Enable/Disable Webcam based on CMOS settings */
	// if (get_uint_option("webcam", 1) == 0)
	//	mcfg->? = 0;

	/* Enable/Disable Fingerprint Reader based on CMOS Settings */
	// if (get_uint_option("fingerprint_reader", 1) == 0)
	//	mcfg->? = 0;

	/* Enable/Disable Card Reader based on CMOS Settings */
	// if (get_uint_option("card_reader", 1) == 0)
	//	mcfg->? = 0;
}
