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

	u8 spd_index = 6;

	printk(BIOS_INFO, "SPD index %d\n", spd_index);

	mainboard_fill_dq_map_data(&mem_cfg->DqByteMapCh0);
	mainboard_fill_dqs_map_data(&mem_cfg->DqsMapCpu2DramCh0);
	mainboard_fill_rcomp_res_data(&mem_cfg->RcompResistor);
	mainboard_fill_rcomp_strength_data(&mem_cfg->RcompTarget);

	struct region_device spd_rdev;

	mem_cfg->DqPinsInterleaved = 0;
	if (get_spd_cbfs_rdev(&spd_rdev, spd_index) < 0)
		die("spd.bin not found\n");
	mem_cfg->MemorySpdDataLen = region_device_sz(&spd_rdev);
	/* Memory leak is ok since we have memory mapped boot media */
	// TODO evaluate google/eve way of loading
	mem_cfg->MemorySpdPtr00 = (uintptr_t)rdev_mmap_full(&spd_rdev);
	mem_cfg->MemorySpdPtr10 = mem_cfg->MemorySpdPtr00;

	mupd->FspmTestConfig.DmiVc1 = 1;
}
