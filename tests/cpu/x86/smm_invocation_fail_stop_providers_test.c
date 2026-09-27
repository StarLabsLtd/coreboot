/* SPDX-License-Identifier: GPL-2.0-only */

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <soc/intel/common/tco.h>

void q35_fail_stop(void);
void starbook_mtl_fail_stop(void);

static jmp_buf terminal_halt;
static char events[32];
static size_t event_count;
static uint16_t tco_control = TCO1_TMR_HLT | 0x20;
static uint16_t tco_timer = 0xa800;

#define expect(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static void record(char event)
{
	expect(event_count < sizeof(events));
	events[event_count++] = event;
}

void do_full_reset(void)
{
	record('F');
}

void do_system_reset(void)
{
	record('S');
}

uint32_t tco_get_timer_min_value(void)
{
	record('M');
	return 2;
}

uint16_t tco_read_reg(uint16_t reg)
{
	if (reg == TCO_TMR) {
		record('A');
		return tco_timer;
	}
	expect(reg == TCO1_CNT);
	record('R');
	return tco_control;
}

void tco_write_reg(uint16_t reg, uint16_t value)
{
	switch (reg) {
	case TCO_TMR:
		expect(value == ((tco_timer & ~TCO_TMR_MASK) | 2));
		record('T');
		break;
	case TCO1_STS:
		expect(value == TCO1_STS_TIMEOUT);
		record('I');
		break;
	case TCO2_STS:
		expect(value == TCO2_STS_SECOND_TO);
		record('C');
		break;
	case TCO1_CNT:
		expect(value == (tco_control & ~TCO1_TMR_HLT));
		record('E');
		break;
	case TCO_RLD:
		expect(value == 1);
		record('L');
		break;
	default:
		expect(false);
	}
}

void halt(void)
{
	record('H');
	longjmp(terminal_halt, 1);
}

static void expect_sequence(void (*provider)(void), const char *expected)
{
	event_count = 0;
	memset(events, 0, sizeof(events));
	if (setjmp(terminal_halt) == 0) {
		provider();
		expect(false);
	}
	expect(event_count == strlen(expected));
	expect(memcmp(events, expected, event_count) == 0);
}

int main(void)
{
	expect_sequence(q35_fail_stop, "FSH");
	expect_sequence(starbook_mtl_fail_stop, "SAMTICRELH");
	return 0;
}
