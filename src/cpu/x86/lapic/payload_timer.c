/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/coreboot_tables.h>
#include <console/console.h>
#include <cpu/x86/lapic.h>
#include <cpu/x86/tsc.h>
#include <timer.h>

#define CALIBRATION_USECS 10000
#define CALIBRATION_POLL_LIMIT 10000000

uint32_t __weak measured_lapic_timer_hz(void)
{
	uint32_t saved_lvtt, saved_tdcr, saved_tmict, start_lapic, end_lapic;
	uint32_t polls;
	unsigned long frequency_mhz;
	uint64_t start_tsc, end_tsc, elapsed;
	uint64_t frequency;

	/* Never calibrate against a guessed rate or the timer being measured. */
	if (!CONFIG(TSC_MONOTONIC_TIMER) || CONFIG(UNKNOWN_TSC_RATE) ||
	    CONFIG(LAPIC_MONOTONIC_TIMER))
		return 0;
	frequency_mhz = tsc_freq_mhz();
	if (!frequency_mhz)
		return 0;

	saved_lvtt = lapic_read(LAPIC_LVTT);
	saved_tdcr = lapic_read(LAPIC_TDCR);
	saved_tmict = lapic_read(LAPIC_TMICT);
	lapic_write(LAPIC_LVTT, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_TDCR, LAPIC_TDR_DIV_1);
	lapic_write(LAPIC_TMICT, UINT32_MAX);
	start_tsc = rdtscll();
	start_lapic = lapic_read(LAPIC_TMCCT);
	for (polls = 0; polls < CALIBRATION_POLL_LIMIT; polls++) {
		end_tsc = rdtscll();
		if ((end_tsc - start_tsc) / frequency_mhz >= CALIBRATION_USECS)
			break;
	}
	end_lapic = lapic_read(LAPIC_TMCCT);
	lapic_write(LAPIC_LVTT, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_TDCR, saved_tdcr);
	lapic_write(LAPIC_TMICT, saved_tmict);
	lapic_write(LAPIC_LVTT, saved_lvtt);

	/* Use the complete interval, without accumulating per-poll fractional loss. */
	elapsed = (end_tsc - start_tsc) / frequency_mhz;
	if (polls == CALIBRATION_POLL_LIMIT || !elapsed || start_lapic <= end_lapic)
		return 0;
	frequency = (uint64_t)(start_lapic - end_lapic) * USECS_PER_SEC / elapsed;
	return frequency > UINT32_MAX ? 0 : frequency;
}

void lb_add_local_apic_timer(struct lb_header *header)
{
	struct lb_local_apic_timer_info *timer;
	uint32_t frequency;

	if (!(rdmsr(LAPIC_BASE_MSR).lo & LAPIC_BASE_MSR_ENABLE))
		return;
	frequency = measured_lapic_timer_hz();

	if (!frequency) {
		printk(BIOS_ERR, "Could not measure local APIC timer frequency\n");
		return;
	}
	timer = (void *)lb_new_record(header);
	timer->tag = LB_TAG_LOCAL_APIC_TIMER_INFO;
	timer->size = sizeof(*timer);
	timer->revision = 1;
	timer->reserved = 0;
	timer->frequency_hz = frequency;
	printk(BIOS_INFO, "Local APIC timer measured at %u Hz\n", frequency);
}
