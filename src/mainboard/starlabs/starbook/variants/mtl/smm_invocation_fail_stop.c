/* SPDX-License-Identifier: GPL-2.0-only */

#include <cf9_reset.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <halt.h>
#include <intelblocks/tco.h>
#include <soc/intel/common/tco.h>

static void arm_tco_watchdog(void)
{
	uint16_t control;
	uint16_t timer;

	timer = tco_read_reg(TCO_TMR);
	timer &= ~TCO_TMR_MASK;
	timer |= tco_get_timer_min_value();
	tco_write_reg(TCO_TMR, timer);
	tco_write_reg(TCO1_STS, TCO1_STS_TIMEOUT);
	tco_write_reg(TCO2_STS, TCO2_STS_SECOND_TO);
	control = tco_read_reg(TCO1_CNT);
	tco_write_reg(TCO1_CNT, control & ~TCO1_TMR_HLT);
	tco_write_reg(TCO_RLD, 1);
}

void smm_invocation_platform_fail_stop(void)
{
	do_system_reset();
	arm_tco_watchdog();
	halt();
}
