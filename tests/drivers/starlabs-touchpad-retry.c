/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <types.h>

int starlabs_touchpad_retry_for_test(int (*op)(void *arg), void *arg,
				     int retries, unsigned int delay_ms);

struct retry_state {
	int calls;
	int failures;
};

static int delay_calls;
static unsigned int last_delay_ms;

void mdelay(unsigned int msecs)
{
	delay_calls++;
	last_delay_ms = msecs;
}

static int scripted_operation(void *arg)
{
	struct retry_state *state = arg;

	state->calls++;
	return state->calls <= state->failures ? -1 : 0;
}

static int setup(void **state)
{
	delay_calls = 0;
	last_delay_ms = 0;
	return 0;
}

static void immediate_success_has_no_delay(void **state)
{
	struct retry_state retry = { .failures = 0 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		3, 20), 0);
	assert_int_equal(retry.calls, 1);
	assert_int_equal(delay_calls, 0);
}

static void final_failure_has_only_between_attempt_delays(void **state)
{
	struct retry_state retry = { .failures = 3 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		3, 20), -1);
	assert_int_equal(retry.calls, 3);
	assert_int_equal(delay_calls, 2);
	assert_int_equal(last_delay_ms, 20);
}

static void success_after_retry_has_expected_delays(void **state)
{
	struct retry_state retry = { .failures = 2 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		3, 5), 0);
	assert_int_equal(retry.calls, 3);
	assert_int_equal(delay_calls, 2);
	assert_int_equal(last_delay_ms, 5);
}

static void one_attempt_failure_has_no_terminal_delay(void **state)
{
	struct retry_state retry = { .failures = 1 };

	assert_int_equal(starlabs_touchpad_retry_for_test(scripted_operation, &retry,
		1, 20), -1);
	assert_int_equal(retry.calls, 1);
	assert_int_equal(delay_calls, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(immediate_success_has_no_delay, setup),
		cmocka_unit_test_setup(final_failure_has_only_between_attempt_delays, setup),
		cmocka_unit_test_setup(success_after_retry_has_expected_delays, setup),
		cmocka_unit_test_setup(one_attempt_failure_has_no_terminal_delay, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
