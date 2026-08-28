/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_OPTION_CFR_RUNTIME_H
#define DRIVERS_OPTION_CFR_RUNTIME_H

#include <commonlib/bsd/cb_err.h>
#include <types.h>

/*
 * Boards that advertise runtime-apply metadata must implement this hook in
 * SMM, not ramstage, for the published runtime token IDs. The common handler
 * reports the returned status through APM_STS as a signed 8-bit cb_err subset:
 * CB_SUCCESS, CB_ERR, CB_ERR_ARG or CB_ERR_NOT_IMPLEMENTED.
 *
 * Board callbacks must validate the token, re-read and validate the stored
 * option value, and avoid unbounded or destructive work in SMM.
 */
enum cb_err cfr_runtime_apply_option(uint32_t id);
void cfr_runtime_apply_smi(void);

#endif /* DRIVERS_OPTION_CFR_RUNTIME_H */
