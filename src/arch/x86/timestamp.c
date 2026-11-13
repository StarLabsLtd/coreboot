/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/tsc.h>
#include <stdint.h>
#include <timestamp.h>

uint64_t timestamp_get(void)
{
	return rdtscll();
}

int timestamp_tick_freq_mhz(void)
{
	unsigned long frequency;

	/* Chipsets that have a constant TSC provide this value correctly. */
	if (tsc_constant_rate())
		frequency = tsc_freq_mhz();
	else if (CONFIG(SERIALIZE_MEASURED_TSC_RATE))
		frequency = tsc_freq_mhz_measured();
	else
		return 0;

	/* A zero rate leaves conversion to userspace; the serialized field is 16-bit. */
	return frequency > UINT16_MAX ? 0 : frequency;
}
