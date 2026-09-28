/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <cpu/x86/smm_invocation_tuple_trigger.h>

_Static_assert(SMM_APMC_AUTHVAR_PRESENCE == UINT8_MAX,
	"invocation sentinel must carry the private command in AL");
_Static_assert(SMM_APMC_AUTHVAR_PRESENCE_SENTINEL == UINT64_MAX,
	"i386 invocation tuple encoding requires an all-ones sentinel");

uint64_t smm_invocation_tuple_trigger(void)
{
	uint32_t eax = UINT32_MAX;
	uint32_t ecx = UINT32_MAX;
	uint16_t dx = APM_CNT;

	__asm__ __volatile__("outb %%al, %%dx"
		: "+a" (eax), "+c" (ecx)
		: "d" (dx)
		: "memory");

	return ((uint64_t)ecx << 32) | eax;
}
