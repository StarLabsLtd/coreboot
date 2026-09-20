/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/lapic.h>

#include "lapic_timer.h"

#define PM_MASK 0x00ffffffU
#define PM_TARGET 35795U

static uint32_t registers[4];
static uint32_t pm_start, pm_step, pm_reads;
static uint32_t lapic_start, lapic_end, current_reads;
static uint32_t write_registers[8], write_values[8], write_count;

static size_t register_index(uint32_t reg)
{
	switch (reg) {
	case LAPIC_LVTT: return 0;
	case LAPIC_TDCR: return 1;
	case LAPIC_TMICT: return 2;
	default: return 3;
	}
}

static uint32_t mock_lapic_read(uint32_t reg)
{
	if (reg == LAPIC_TMCCT)
		return current_reads++ == 0U ? lapic_start : lapic_end;
	return registers[register_index(reg)];
}

static void mock_lapic_write(uint32_t reg, uint32_t value)
{
	if (write_count < 8U) {
		write_registers[write_count] = reg;
		write_values[write_count] = value;
	}
	write_count++;
	registers[register_index(reg)] = value;
}

static uint32_t mock_pm_read(void)
{
	uint32_t value = (pm_start + pm_reads * pm_step) & PM_MASK;
	pm_reads++;
	return value;
}

static const struct q35_lapic_timer_ops ops = {
	mock_lapic_read, mock_lapic_write, mock_pm_read
};

static void reset_fixture(void)
{
	registers[0] = 0x12345678U;
	registers[1] = 0x9abcdef0U;
	registers[2] = 0x13579bdfU;
	pm_start = PM_MASK - 100U;
	pm_step = PM_TARGET;
	pm_reads = current_reads = write_count = 0U;
	lapic_start = UINT32_MAX;
	lapic_end = UINT32_MAX - 10000000U;
}

static int registers_restored(void)
{
	return registers[0] == 0x12345678U && registers[1] == 0x9abcdef0U &&
		registers[2] == 0x13579bdfU;
}

static int programming_sequence_valid(void)
{
	return write_count == 7U &&
		write_registers[0] == LAPIC_LVTT &&
		write_values[0] == LAPIC_LVT_MASKED &&
		write_registers[1] == LAPIC_TDCR &&
		write_values[1] == LAPIC_TDR_DIV_1 &&
		write_registers[2] == LAPIC_TMICT &&
		write_values[2] == UINT32_MAX &&
		write_registers[3] == LAPIC_LVTT &&
		write_values[3] == LAPIC_LVT_MASKED &&
		write_registers[4] == LAPIC_TDCR &&
		write_values[4] == 0x9abcdef0U &&
		write_registers[5] == LAPIC_TMICT &&
		write_values[5] == 0x13579bdfU &&
		write_registers[6] == LAPIC_LVTT &&
		write_values[6] == 0x12345678U;
}

static int check(int condition, const char *message)
{
	(void)message;
	return !condition;
}

int main(void)
{
	uint32_t frequency;
	int failures = 0;

	reset_fixture();
	frequency = q35_measure_lapic_timer_hz(&ops, 4U);
	failures += check(frequency > 999000000U && frequency < 1001000000U,
		"24-bit wrap measurement was inaccurate");
	failures += check(registers_restored(), "success did not restore LAPIC state");
	failures += check(programming_sequence_valid(),
		"calibration programming/restoration sequence was incorrect");
	reset_fixture();
	failures += check(q35_measure_lapic_timer_hz(&ops, 0U) == 0U,
		"zero poll limit was accepted");
	failures += check(registers_restored() && programming_sequence_valid(),
		"zero poll limit did not restore LAPIC state");
	reset_fixture();
	pm_step = 0U;
	failures += check(q35_measure_lapic_timer_hz(&ops, 3U) == 0U,
		"stalled PM timer was accepted");
	failures += check(registers_restored(), "stall did not restore LAPIC state");
	reset_fixture();
	lapic_end = lapic_start;
	failures += check(q35_measure_lapic_timer_hz(&ops, 4U) == 0U,
		"zero LAPIC delta was accepted");
	failures += check(registers_restored(),
		"zero LAPIC delta did not restore LAPIC state");
	reset_fixture();
	lapic_end = 0U;
	failures += check(q35_measure_lapic_timer_hz(&ops, 4U) == 0U,
		"overflowing LAPIC frequency was accepted");
	failures += check(registers_restored(), "overflow did not restore LAPIC state");
	return failures == 0 ? 0 : 1;
}
