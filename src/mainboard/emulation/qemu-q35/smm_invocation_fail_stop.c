/* SPDX-License-Identifier: GPL-2.0-only */

#include <cf9_reset.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <halt.h>

void smm_invocation_platform_fail_stop(void)
{
	do_full_reset();
	do_system_reset();
	halt();
}
