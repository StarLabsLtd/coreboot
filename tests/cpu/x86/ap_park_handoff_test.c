/* SPDX-License-Identifier: GPL-2.0-only */

#include <setjmp.h>

/* The canonical bootstate header declares coreboot's void main, not host main. */
#define main coreboot_ramstage_main
#include <bootstate.h>
#undef main
#include <console/console.h>
#include <cpu/x86/mp.h>

/* Only the MP admission result and fatal platform sink are modeled here. */
static jmp_buf terminal;
static enum cb_err park_result;
static unsigned int park_calls;
static unsigned int terminal_calls;

enum cb_err mp_park_aps(void)
{
	park_calls++;
	return park_result;
}

void die(const char *format, ...)
{
	(void)format;
	terminal_calls++;
	longjmp(terminal, 1);
}

int main(void)
{
	park_result = CB_SUCCESS;
	if (setjmp(terminal))
		return 1;
	arch_bootstate_coreboot_exit();
	if (park_calls != CONFIG(PARALLEL_MP_AP_WORK) || terminal_calls)
		return 2;

	park_calls = 0;
	park_result = CB_ERR;
	if (!setjmp(terminal)) {
		arch_bootstate_coreboot_exit();
		if (CONFIG(PARALLEL_MP_AP_WORK))
			return 3;
	}
	if (park_calls != CONFIG(PARALLEL_MP_AP_WORK) ||
	    terminal_calls != CONFIG(PARALLEL_MP_AP_WORK))
		return 4;
	return 0;
}
