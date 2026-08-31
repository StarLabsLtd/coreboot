/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <boot/coreboot_tables.h>
#include <console/console.h>
#include <cpu/x86/lapic.h>
#include <stdint.h>
#include <southbridge/intel/i82801ix/i82801ix.h>

#include "lapic_timer.h"

#define ACPI_PM_TIMER_FREQUENCY_HZ 3579545U
#define ACPI_PM_TIMER_MASK 0x00ffffffU
#define ACPI_PM_TIMER_OFFSET 0x08U
#define CALIBRATION_PM_TICKS (ACPI_PM_TIMER_FREQUENCY_HZ / 100U)
#define CALIBRATION_POLL_LIMIT 10000000U

static uint32_t elapsed24(uint32_t start, uint32_t end)
{
	return (end - start) & ACPI_PM_TIMER_MASK;
}

uint32_t q35_measure_lapic_timer_hz(const struct q35_lapic_timer_ops *ops,
	uint32_t poll_limit)
{
	const uint32_t saved_lvtt = ops->lapic_read(LAPIC_LVTT);
	const uint32_t saved_tdcr = ops->lapic_read(LAPIC_TDCR);
	const uint32_t saved_tmict = ops->lapic_read(LAPIC_TMICT);
	uint32_t start_pm, end_pm, start_lapic, end_lapic;
	uint32_t pm_ticks, lapic_ticks, polls;
	uint64_t frequency;

	ops->lapic_write(LAPIC_LVTT, LAPIC_LVT_MASKED);
	ops->lapic_write(LAPIC_TDCR, LAPIC_TDR_DIV_1);
	ops->lapic_write(LAPIC_TMICT, UINT32_MAX);
	start_pm = ops->pm_timer_read() & ACPI_PM_TIMER_MASK;
	end_pm = start_pm;
	start_lapic = ops->lapic_read(LAPIC_TMCCT);
	for (polls = 0; polls < poll_limit; polls++) {
		end_pm = ops->pm_timer_read() & ACPI_PM_TIMER_MASK;
		if (elapsed24(start_pm, end_pm) >= CALIBRATION_PM_TICKS)
			break;
	}
	end_lapic = ops->lapic_read(LAPIC_TMCCT);

	ops->lapic_write(LAPIC_LVTT, LAPIC_LVT_MASKED);
	ops->lapic_write(LAPIC_TDCR, saved_tdcr);
	ops->lapic_write(LAPIC_TMICT, saved_tmict);
	ops->lapic_write(LAPIC_LVTT, saved_lvtt);

	if (polls == poll_limit)
		return 0;
	pm_ticks = elapsed24(start_pm, end_pm);
	lapic_ticks = start_lapic - end_lapic;
	if (pm_ticks == 0 || lapic_ticks == 0)
		return 0;
	frequency = (uint64_t)lapic_ticks * ACPI_PM_TIMER_FREQUENCY_HZ / pm_ticks;
	if (frequency == 0 || frequency > UINT32_MAX)
		return 0;
	return frequency;
}

#ifndef __TEST__
static uint32_t hardware_lapic_read(uint32_t reg) { return lapic_read(reg); }
static void hardware_lapic_write(uint32_t reg, uint32_t value)
{
	lapic_write(reg, value);
}
static uint32_t hardware_pm_timer_read(void)
{
	return inl(DEFAULT_PMBASE + ACPI_PM_TIMER_OFFSET);
}

uint64_t soc_local_apic_timer_frequency_hz(void)
{
	static const struct q35_lapic_timer_ops ops = {
		hardware_lapic_read, hardware_lapic_write, hardware_pm_timer_read
	};
	uint32_t frequency_hz = q35_measure_lapic_timer_hz(&ops,
		CALIBRATION_POLL_LIMIT);

	if (frequency_hz == 0) {
		printk(BIOS_ERR, "QEMU: failed to calibrate local APIC timer\n");
		return 0;
	}
	printk(BIOS_INFO, "QEMU: local APIC timer measured at %u Hz\n", frequency_hz);
	return frequency_hz;
}
#endif
