/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _SOC_RAMSTAGE_H_
#define _SOC_RAMSTAGE_H_

#include <commonlib/bsd/cb_err.h>
#include <fsp/api.h>
#include <fsp/util.h>
#include <soc/soc_chip.h>

void mainboard_silicon_init_params(FSP_S_CONFIG *params);
void mainboard_update_soc_chip_config(struct soc_intel_meteorlake_config *config);
void soc_init_pre_device(void *chip_info);
enum cb_err mainboard_mor_early_dma_prepare(void);

#endif
