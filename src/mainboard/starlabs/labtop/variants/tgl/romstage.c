/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <baseboard/variants.h>
#include <console/console.h>
#include <fsp/api.h>
#include <option.h>
#include <soc/meminit.h>
#include <soc/romstage.h>
#include <spd_bin.h>
#include <types.h>

static const struct mb_cfg ddr4_mem_config = {
	.type = MEM_TYPE_DDR4,

	/* .rcomp = {
		.resistor = 100,
		.targets = {40, 30, 33, 33, 30},
	}, */

	.ect = false, /* Early Command Training */

	.ddr4_config = {
		.dq_pins_interleaved = false,
	}
};

void mainboard_memory_init_params(FSPM_UPD *mupd)
{
	const struct mb_cfg *mem_config = &ddr4_mem_config;
	const bool half_populated = false;

	const struct mem_spd ddr4_spd_info = {
		.topo = MEM_TOPO_DIMM_MODULE,
		.smbus = {
			[0] = {
				.addr_dimm[0] = 0x50,
//				.addr_dimm[1] = 0x51,
			},
			[1] = {
				.addr_dimm[0] = 0x52,
//				.addr_dimm[1] = 0x53,
			},
		},
	};

	memcfg_init(&mupd->FspmConfig, mem_config, &ddr4_spd_info, half_populated);

	const uint8_t ht = get_uint_option("hyper_threading",
		mupd->FspmConfig.HyperThreading);
	mupd->FspmConfig.HyperThreading = ht;

};
