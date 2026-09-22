/* SPDX-License-Identifier: GPL-2.0-only */

#include <limits.h>
#include <tests/test.h>
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

static void test_serialized_frequency_bounds(void **state)
{
	assert_int_equal(0, timestamp_validate_tick_freq_mhz(0));
	assert_int_equal(1, timestamp_validate_tick_freq_mhz(1));
	assert_int_equal(UINT16_MAX,
			 timestamp_validate_tick_freq_mhz(UINT16_MAX));
	assert_int_equal(0, timestamp_validate_tick_freq_mhz(UINT16_MAX + 1UL));
	assert_int_equal(0, timestamp_validate_tick_freq_mhz(ULONG_MAX));
}

static void test_qemu_measured_frequency(void **state)
{
	measured_frequency = 2400;
	assert_int_equal(2400, timestamp_tick_freq_mhz());
	measured_frequency = 0;
	assert_int_equal(0, timestamp_tick_freq_mhz());
	measured_frequency = UINT16_MAX + 1UL;
	assert_int_equal(0, timestamp_tick_freq_mhz());
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_serialized_frequency_bounds),
		cmocka_unit_test(test_qemu_measured_frequency),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
