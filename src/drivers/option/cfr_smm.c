/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <drivers/option/cfr_runtime.h>

__weak enum cb_err cfr_runtime_apply_option(uint32_t id)
{
	static bool warned;

	if (!warned) {
		printk(BIOS_WARNING, "CFR: runtime apply has no board SMM handler\n");
		warned = true;
	}

	return CB_ERR_NOT_IMPLEMENTED;
}

static uint8_t cfr_runtime_apply_status(enum cb_err ret)
{
	switch (ret) {
	case CB_SUCCESS:
	case CB_ERR:
	case CB_ERR_ARG:
	case CB_ERR_NOT_IMPLEMENTED:
		return (uint8_t)ret;
	default:
		printk(BIOS_WARNING, "CFR: unsupported runtime apply status %d\n", ret);
		return (uint8_t)CB_ERR;
	}
}

void cfr_runtime_apply_smi(void)
{
	enum cb_err ret;

	ret = cfr_runtime_apply_option(inb(APM_STS));
	outb(cfr_runtime_apply_status(ret), APM_STS);
}
