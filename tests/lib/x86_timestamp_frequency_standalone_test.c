/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/timestamp_serialized.h>
#include <cpu/x86/tsc.h>
#include <limits.h>
#include <timestamp.h>

static unsigned long measured_frequency;

unsigned long tsc_freq_mhz(void)
{
	return measured_frequency;
}

unsigned long tsc_freq_mhz_measured(void)
{
	return measured_frequency;
}

static int serialized_frequency_is(unsigned long measured, uint16_t expected)
{
	struct timestamp_table table = { 0 };

	measured_frequency = measured;
	table.tick_freq_mhz = timestamp_tick_freq_mhz();
	return table.tick_freq_mhz == expected;
}

int main(void)
{
	unsigned long cached = 0;

	if (!CONFIG(UNKNOWN_TSC_RATE) || !CONFIG(SERIALIZE_MEASURED_TSC_RATE))
		return 1;
	if (!serialized_frequency_is(2400, 2400))
		return 2;
	if (!serialized_frequency_is(0, 0))
		return 3;
	if (!serialized_frequency_is(UINT16_MAX, UINT16_MAX))
		return 4;
	if (!serialized_frequency_is(UINT16_MAX + 1UL, 0))
		return 5;
	if (!serialized_frequency_is(ULONG_MAX, 0))
		return 6;
	if (tsc_cache_measured_frequency(&cached, 0) != 0)
		return 7;
	if (tsc_cache_measured_frequency(&cached, 2400) != 2400)
		return 8;
	if (tsc_cache_measured_frequency(&cached, 1800) != 2400)
		return 9;
	return 0;
}
