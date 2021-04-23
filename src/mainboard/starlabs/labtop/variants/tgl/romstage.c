/* SPDX-License-Identifier: GPL-2.0-only */

#include <fsp/util.h>
#include <soc/meminit.h>
#include <soc/romstage.h>

static const struct mb_ddr4_cfg board_cfg = {
	.dq_pins_interleaved = 0,
	.ect = 0,
};

static const struct spd_info spd = {
	.topology = MIXED,
	.md_spd_loc = SPD_CBFS,
	.cbfs_index = 0,
	.smbus_info[1] = {
		.addr_dimm0 = 0x52,
	},
};

void mainboard_memory_init_params(FSPM_UPD *mupd)
{
	mupd->FspmConfig.SaOcSupport = 1;
	const bool half_populated = false;
	meminit_ddr4(&mupd->FspmConfig, &board_cfg, &spd, half_populated);
}
