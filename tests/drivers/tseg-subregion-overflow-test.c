/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <setjmp.h>

static uintptr_t region_base;
static size_t region_size;
static bool die_called;
static jmp_buf die_jmp;

void smm_region(uintptr_t *start, size_t *size)
{
	*start = region_base;
	*size = region_size;
}

void __noreturn die(const char *message, ...)
{
	die_called = true;
	longjmp(die_jmp, 1);
}

int main(void)
{
	uintptr_t base = 0;
	size_t size = 0;

	region_base = 0;
	region_size = 0x100000;
	if (setjmp(die_jmp) == 0) {
		smm_subregion(SMM_SUBREGION_HANDLER, &base, &size);
		return 1;
	}

	return die_called && base == 0 && size == 0 ? 0 : 1;
}
