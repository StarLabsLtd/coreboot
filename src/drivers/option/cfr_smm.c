/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <cpu/x86/smm.h>
#include <drivers/option/cfr_runtime.h>

__weak enum cb_err cfr_runtime_apply_option(uint32_t id)
{
	return CB_ERR_NOT_IMPLEMENTED;
}

void cfr_runtime_apply_smi(void)
{
	enum cb_err ret;

	ret = cfr_runtime_apply_option(inb(APM_STS));
	outb((uint8_t)ret, APM_STS);
}
