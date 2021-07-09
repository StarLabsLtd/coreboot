/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <option.h>
#include <soc/ramstage.h>

void mainboard_silicon_test_config(FSP_S_TEST_CONFIG *tconfig)
{
	const uint8_t vtd = get_uint_option("vtd", 1);
	tconfig->VtdDisable = !vtd;
}
