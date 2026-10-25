/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <cpu/x86/lapic.h>
#include <southbridge/intel/i82801ix/i82801ix.h>

#define ACPI_PM_TIMER_FREQUENCY_HZ 3579545U
#define ACPI_PM_TIMER_MASK 0x00ffffffU
#define ACPI_PM_TIMER_OFFSET 0x08U
#define CALIBRATION_PM_TICKS (ACPI_PM_TIMER_FREQUENCY_HZ / 100U)
#define CALIBRATION_POLL_LIMIT 10000000U

uint32_t measured_lapic_timer_hz(void)
{
	const uint32_t saved_lvtt = lapic_read(LAPIC_LVTT);
	const uint32_t saved_tdcr = lapic_read(LAPIC_TDCR);
	const uint32_t saved_tmict = lapic_read(LAPIC_TMICT);
	uint32_t start_pm, end_pm, start_lapic, end_lapic;
	uint32_t pm_ticks, lapic_ticks, polls;
	uint64_t frequency;

	/* QEMU's TSC calibration can fall back to a guessed rate. Use ICH9 instead. */
	lapic_write(LAPIC_LVTT, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_TDCR, LAPIC_TDR_DIV_1);
	lapic_write(LAPIC_TMICT, UINT32_MAX);
	start_pm = inl(DEFAULT_PMBASE + ACPI_PM_TIMER_OFFSET) & ACPI_PM_TIMER_MASK;
	end_pm = start_pm;
	start_lapic = lapic_read(LAPIC_TMCCT);
	for (polls = 0; polls < CALIBRATION_POLL_LIMIT; polls++) {
		end_pm = inl(DEFAULT_PMBASE + ACPI_PM_TIMER_OFFSET) & ACPI_PM_TIMER_MASK;
		if (((end_pm - start_pm) & ACPI_PM_TIMER_MASK) >= CALIBRATION_PM_TICKS)
			break;
	}
	end_lapic = lapic_read(LAPIC_TMCCT);

	lapic_write(LAPIC_LVTT, LAPIC_LVT_MASKED);
	lapic_write(LAPIC_TDCR, saved_tdcr);
	lapic_write(LAPIC_TMICT, saved_tmict);
	lapic_write(LAPIC_LVTT, saved_lvtt);

	if (polls == CALIBRATION_POLL_LIMIT)
		return 0;
	pm_ticks = (end_pm - start_pm) & ACPI_PM_TIMER_MASK;
	lapic_ticks = start_lapic - end_lapic;
	if (!pm_ticks || !lapic_ticks || start_lapic <= end_lapic)
		return 0;
	frequency = (uint64_t)lapic_ticks * ACPI_PM_TIMER_FREQUENCY_HZ / pm_ticks;
	return frequency > UINT32_MAX ? 0 : frequency;
}
