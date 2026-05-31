/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_OPTION_CFR_RUNTIME_H
#define DRIVERS_OPTION_CFR_RUNTIME_H

#include <types.h>

/*
 * Boards that advertise runtime-apply metadata implement this hook for the
 * published runtime token IDs. The common handler reports the returned status
 * through APM_STS.
 */
enum cb_err cfr_runtime_apply_option(uint32_t id);
void cfr_runtime_apply_smi(void);

#endif /* DRIVERS_OPTION_CFR_RUNTIME_H */
