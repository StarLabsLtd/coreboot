/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include DIAGNOSTIC_IMPLEMENTATION_SOURCE
#include <assert.h>

int main(void)
{
	assert(diagnostic_text_length(NULL) == 0);
	assert(diagnostic_text_length("") == 0);
	assert(diagnostic_text_length("formatter") == 9);
	assert(diagnostic_length_add(0, 0) == 0);
	assert(diagnostic_length_add(MAX_UINTN, 1) == MAX_UINTN);
	assert(diagnostic_length_add(MAX_UINTN - 1, 1) == MAX_UINTN);
	assert(diagnostic_length_add(MAX_UINTN - 1, 2) == MAX_UINTN);
	assert(diagnostic_decimal_length(0) == 1);
	assert(diagnostic_decimal_length(9) == 1);
	assert(diagnostic_decimal_length(10) == 2);
	assert(diagnostic_decimal_length(MAX_UINT64) == 20);
	active.clock_hz = 0;
	assert(diagnostic_elapsed_microseconds(MAX_UINT64) == 0);
	active.clock_hz = 1;
	assert(diagnostic_elapsed_microseconds(MAX_UINT64) == MAX_UINT64);
	active.clock_hz = 1000000;
	assert(diagnostic_elapsed_microseconds(MAX_UINT64) == MAX_UINT64);
	assert(diagnostic_elapsed_microseconds(1000001) == 1000001);
	active.clock_hz = MAX_UINT64;
	/* Preserve the existing guarded fractional-product overflow policy. */
	assert(diagnostic_elapsed_microseconds(MAX_UINT64 - 1) == 0);
	assert(diagnostic_elapsed_microseconds(MAX_UINT64) == 1000000);
	return 0;
}
