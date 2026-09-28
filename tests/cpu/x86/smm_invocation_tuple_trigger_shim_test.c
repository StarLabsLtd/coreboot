/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>

#define TEST_LOW 0x89abcdefU
#define TEST_HIGH 0x01234567U

static unsigned int calls;

void tuple_trigger_test_hook(uint32_t *eax, uint32_t *ecx, uint16_t dx)
{
	if (*eax != UINT32_MAX || *ecx != UINT32_MAX || dx != 0xb2U)
		__builtin_trap();
	*eax = TEST_LOW;
	*ecx = TEST_HIGH;
	calls++;
}

uint64_t smm_invocation_tuple_trigger(void);

int main(void)
{
	return smm_invocation_tuple_trigger() ==
		(((uint64_t)TEST_HIGH << 32) | TEST_LOW) && calls == 1U ? 0 : 1;
}
