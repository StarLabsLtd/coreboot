/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOC_INTEL_COMMON_BLOCK_SMM_SAVE_STATE_OPS_H
#define SOC_INTEL_COMMON_BLOCK_SMM_SAVE_STATE_OPS_H

#include <cpu/x86/save_state.h>

/* SoCs may override the common handler's default save-state layout. */
const struct smm_save_state_ops *get_smm_save_state_ops(void);
extern const struct smm_save_state_ops em64t100_smm_ops;
extern const struct smm_save_state_ops em64t101_smm_ops;

#endif
