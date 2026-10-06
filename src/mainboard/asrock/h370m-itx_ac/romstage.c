/* SPDX-License-Identifier: GPL-2.0-only */

#include <soc/cnl_memcfg_init.h>
#include <soc/romstage.h>

#include <option.h>

static const struct cnl_mb_cfg baseboard_mem_cfg = {
	/* Access memory info through SMBUS. */
	.spd[0] = {.read_type = READ_SMBUS, .spd_spec = {.spd_smbus_address = 0xa0}},
	.spd[1] = {.read_type = READ_SMBUS, .spd_spec = {.spd_smbus_address = 0xa2}},
	.spd[2] = {.read_type = READ_SMBUS, .spd_spec = {.spd_smbus_address = 0xa4}},
	.spd[3] = {.read_type = READ_SMBUS, .spd_spec = {.spd_smbus_address = 0xa6}},

	.rcomp_resistor = {121, 75, 100},
	.rcomp_targets = {60, 26, 20, 20, 26},
	.dq_pins_interleaved = 1,
	.vref_ca_config = 2,
	.ect = 0,
};

void mainboard_memory_init_params(FSPM_UPD *memupd)
{
	memupd->FspmConfig.UserBd = BOARD_TYPE_DESKTOP;

	memupd->FspmConfig.MrcFastBoot =
		get_uint_option_checked("fast_boot", true, OPTION_BOOL);

	cannonlake_memcfg_init(&memupd->FspmConfig, &baseboard_mem_cfg);
}
