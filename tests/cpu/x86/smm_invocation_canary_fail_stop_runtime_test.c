/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/bsd/compiler.h>
#include <setjmp.h>
#include <stdint.h>

static jmp_buf escape;
static unsigned int fail_stop_calls;
static unsigned int die_calls;

void __noreturn smm_invocation_platform_fail_stop(void)
{
	fail_stop_calls++;
	longjmp(escape, 1);
}

void __noreturn die(const char *message)
{
	(void)message;
	die_calls++;
	longjmp(escape, 2);
}

void printk(int level, const char *format, ...)
{
	(void)level;
	(void)format;
}

#define BIOS_DEBUG 0
#define CONFIG(option) CONFIG_##option
#define CONFIG_DEBUG_SMI 0

static void check_canary(uintptr_t actual_canary, uintptr_t expected_canary)
{
#include "canary-fragment.h"
}

int main(void)
{
	int escape_result;

	if (setjmp(escape) != 0)
		return 1;
	check_canary(0x1234U, 0x1234U);
	if (fail_stop_calls || die_calls)
		return 2;
	escape_result = setjmp(escape);
	if (escape_result == 0)
		check_canary(0x1234U, 0x5678U);
	if (escape_result != 1 || fail_stop_calls != 1U || die_calls)
		return 3;
	return 0;
}
