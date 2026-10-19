/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <timer.h>
#include <types.h>

int starlabs_touchpad_retry_for_test(int (*op)(void *arg), void *arg,
				     int retries, unsigned int delay_ms,
				     unsigned int timeout_ms);

struct retry_state {
	int calls;
	int failures;
};

static int delay_calls;
static unsigned int last_delay_ms;
static int64_t now_us;
static int64_t operation_time_us;

void timer_monotonic_get(struct mono_time *mt)
{
	mt->microseconds = now_us;
}

void mdelay(unsigned int msecs)
{
	delay_calls++;
	last_delay_ms = msecs;
	now_us += msecs * USECS_PER_MSEC;
}

static int scripted_operation(void *arg)
{
	struct retry_state *state = arg;

	state->calls++;
	now_us += operation_time_us;
	return state->calls <= state->failures ? -1 : 0;
}

static int setup(void **state)
{
	delay_calls = 0;
	last_delay_ms = 0;
	now_us = 0;
	operation_time_us = 0;
	return 0;
}

static void immediate_success_has_no_delay(void **state)
{
	struct retry_state retry = { .failures = 0 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		3, 20, 0), 0);
	assert_int_equal(retry.calls, 1);
	assert_int_equal(delay_calls, 0);
}

static void final_failure_has_only_between_attempt_delays(void **state)
{
	struct retry_state retry = { .failures = 3 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		3, 20, 0), -1);
	assert_int_equal(retry.calls, 3);
	assert_int_equal(delay_calls, 2);
	assert_int_equal(last_delay_ms, 20);
}

static void success_after_retry_has_expected_delays(void **state)
{
	struct retry_state retry = { .failures = 2 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		3, 5, 0), 0);
	assert_int_equal(retry.calls, 3);
	assert_int_equal(delay_calls, 2);
	assert_int_equal(last_delay_ms, 5);
}

static void one_attempt_failure_has_no_terminal_delay(void **state)
{
	struct retry_state retry = { .failures = 1 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		1, 20, 0), -1);
	assert_int_equal(retry.calls, 1);
	assert_int_equal(delay_calls, 0);
}

static void deadline_stops_new_attempt_after_bounded_overrun(void **state)
{
	struct retry_state retry = { .failures = 3 };

	/* An in-flight operation and its between-attempt delay may cross the deadline. */
	operation_time_us = 60 * USECS_PER_MSEC;
	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		3, 5, 100), -1);
	assert_int_equal(retry.calls, 2);
	assert_int_equal(delay_calls, 1);
	assert_int_equal(now_us, 125 * USECS_PER_MSEC);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(immediate_success_has_no_delay, setup),
		cmocka_unit_test_setup(final_failure_has_only_between_attempt_delays, setup),
		cmocka_unit_test_setup(success_after_retry_has_expected_delays, setup),
		cmocka_unit_test_setup(one_attempt_failure_has_no_terminal_delay, setup),
		cmocka_unit_test_setup(deadline_stops_new_attempt_after_bounded_overrun,
				       setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
