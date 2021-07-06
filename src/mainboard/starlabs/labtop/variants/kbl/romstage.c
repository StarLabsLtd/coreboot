/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <console/console.h>
#include <soc/romstage.h>
#include <spd_bin.h>
#include "spd/spd_util.c"
#include "spd/spd.h"
#include <ec/acpi/ec.h>
#include <stdint.h>

void mainboard_memory_init_params(FSPM_UPD *mupd)
{
	FSP_M_CONFIG *mem_cfg;
	mem_cfg = &mupd->FspmConfig;

	/* Use the correct entry in the SPD table defined in Makefile.inc */
	u8 spd_index = 6;
	printk(BIOS_INFO, "SPD index %d\n", spd_index);

	mainboard_fill_rcomp_res_data(&mem_cfg->RcompResistor);
	mainboard_fill_rcomp_strength_data(&mem_cfg->RcompTarget);

	/* struct region_device spd_rdev; */

	mem_cfg->DqPinsInterleaved = 0;
	mem_cfg->MemorySpdDataLen = CONFIG_DIMM_SPD_SIZE;
	mem_cfg->MemorySpdPtr00 = spd_cbfs_map(spd_index);
	if (!mem_cfg->MemorySpdPtr00)
		die("spd.bin not found\n");
	mem_cfg->MemorySpdPtr10 = mem_cfg->MemorySpdPtr00;

	mupd->FspmTestConfig.DmiVc1 = 1;

	const uint8_t vtd = get_uint_option("vtd", 1);
	memupd->FspmTestConfig.VtdDisable = !vtd;

	const uint8_t ht = get_uint_option("hyper_threading",
		memupd->FspmConfig.HyperThreading);
	memupd->FspmConfig.HyperThreading = ht;
}
