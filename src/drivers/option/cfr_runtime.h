/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef DRIVERS_OPTION_CFR_RUNTIME_H
#define DRIVERS_OPTION_CFR_RUNTIME_H

#include <types.h>

#if CONFIG(DRIVERS_OPTION_CFR_RUNTIME_APPLY)
enum cb_err cfr_runtime_apply_option(uint32_t id);
int cfr_runtime_apply_smi_apmc(uint8_t apmc);
#else
static inline int cfr_runtime_apply_smi_apmc(uint8_t apmc)
{
	return 0;
}
#endif

#endif /* DRIVERS_OPTION_CFR_RUNTIME_H */
