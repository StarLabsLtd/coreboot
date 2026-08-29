/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/tsc.h>
#include <limits.h>
#include <stdint.h>
#include <timestamp.h>

uint64_t timestamp_get(void)
{
	return rdtscll();
}

int timestamp_tick_freq_mhz(void)
{
	/* Chipsets that have a constant TSC provide this value correctly. */
	if (tsc_constant_rate())
		return timestamp_validate_tick_freq_mhz(tsc_freq_mhz());

	/*
	 * Emulated x86 platforms measure the rate against the i8254 at runtime.
	 * Publish that measurement for timestamp conversion without changing the
	 * UNKNOWN_TSC_RATE contract used by delay and CPU reporting code.
	 */
	if (CONFIG(SERIALIZE_MEASURED_TSC_RATE))
		return timestamp_validate_tick_freq_mhz(tsc_freq_mhz_measured());

	/* Filling tick_freq_mhz = 0 in timestamps-table will trigger
	 * userspace utility to try deduce it from the running system.
	 */
	return 0;
}

int timestamp_validate_tick_freq_mhz(unsigned long frequency)
{
	if (frequency == 0 || frequency > UINT16_MAX)
		return 0;

	return frequency;
}
