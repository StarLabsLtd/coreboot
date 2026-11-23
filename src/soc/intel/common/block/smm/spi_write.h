/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_SMM_SPI_WRITE_H
#define SOC_INTEL_COMMON_BLOCK_SMM_SPI_WRITE_H

struct smm_save_state_ops;
void southbridge_smi_store(const struct smm_save_state_ops *save_state_ops);
void smmstore_drain_sync_smi(void);

#endif
