/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <commonlib/bsd/helpers.h>
#include <cpu/x86/msr.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "authvar_protected_region.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint32_t tseg, gsm;
static msr_t capability, base, mask;
static unsigned int capability_reads, register_reads, smrr_reads, drift;

msr_t rdmsr(unsigned int index)
{
	if (index == 0xfeU) {
		capability_reads++;
		return capability;
	}
	smrr_reads++;
	assert(index == 0x1f2U || index == 0x1f3U);
	msr_t result = index == 0x1f2U ? base : mask;
	if (capability_reads == 2U && drift == index)
		result.lo ^= 0x1000U;
	return result;
}

uint32_t pci_read_config32(unsigned int device, unsigned int index)
{
	assert(device == 0 && (index == 0xb8U || index == 0xb4U));
	register_reads++;
	uint32_t result = index == 0xb8U ? tseg : gsm;
	if (capability_reads == 2U && drift == index)
		result ^= 0x100000U;
	return result;
}

static void reset(uint32_t start, uint32_t size)
{
	tseg = start | 1U;
	gsm = (start + size) | 1U;
	capability = (msr_t){ .lo = (1U << 11) | (1U << 14) };
	base = (msr_t){ .lo = start | 6U };
	mask = (msr_t){ .lo = (~(size - 1U) & 0xfffff000U) | 0xc00U };
	capability_reads = 0;
	register_reads = 0;
	smrr_reads = 0;
	drift = 0;
}

static void denied(void)
{
	struct region output = { .offset = 17, .size = 29 };
	const struct region original = output;

	assert(starbook_mtl_authvar_protected_region_read(&output) == CB_ERR);
	assert(!memcmp(&output, &original, sizeof(output)));
}

int main(void)
{
	for (uint32_t size = 8U << 20; size <= (128U << 20); size <<= 1U) {
		struct region output = {0};

		reset(0x80000000U, size);
		assert(starbook_mtl_authvar_protected_region_read(&output) == CB_SUCCESS);
		assert(output.offset == 0x80000000U && output.size == size);
		assert(capability_reads == 2 && register_reads == 4 && smrr_reads == 4);
	}
	for (unsigned int fault = 0; fault < 18; fault++) {
		reset(0x80000000U, 32U << 20);
		switch (fault) {
		case 0:
			tseg &= ~1U;
			break;
		case 1:
			gsm &= ~1U;
			break;
		case 2:
			capability.lo &= ~(1U << 11);
			break;
		case 3:
			capability.lo &= ~(1U << 14);
			break;
		case 4:
			base.hi = 1;
			break;
		case 5:
			mask.hi = 1;
			break;
		case 6:
			base.lo ^= 0x1000U;
			break;
		case 7:
			mask.lo ^= 0x1000U;
			break;
		case 8:
			base.lo = 0x80000000U;
			break;
		case 9:
			mask.lo &= ~(1U << 11);
			break;
		case 10:
			mask.lo &= ~(1U << 10);
			break;
		case 11:
			tseg |= 2U;
			break;
		case 12:
			gsm |= 2U;
			break;
		case 13:
			gsm = tseg;
			break;
		case 14:
			gsm = 0x7f000001U;
			break;
		case 15:
			gsm = 0x81800001U;
			break;
		case 16:
			reset(0x80100000U, 32U << 20);
			break;
		case 17:
			reset(0, 32U << 20);
			break;
		}
		denied();
		if (fault == 2 || fault == 3)
			assert(!smrr_reads && !register_reads);
	}
	const unsigned int changing[] = { 0xb8U, 0xb4U, 0x1f2U, 0x1f3U };
	for (unsigned int index = 0; index < ARRAY_SIZE(changing); index++) {
		reset(0x80000000U, 32U << 20);
		drift = changing[index];
		denied();
	}
	reset(0x80000000U, 32U << 20);
	assert(starbook_mtl_authvar_protected_region_read(NULL) == CB_ERR);
	assert(!capability_reads && !register_reads && !smrr_reads);
	return 0;
}
