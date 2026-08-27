/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/cfr.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <drivers/efi/option.h>
#include <drivers/option/cfr_settings.h>
#include <tests/test.h>

#define TEST_TOKEN 1

static const struct sm_object setting = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_RUNTIME,
	.opt_name = "setting",
	.ui_name = "Setting",
	.default_value = false,
});

static const struct sm_object form = SM_DECLARE_FORM({
	.ui_name = "Settings",
	.obj_list = (const struct sm_object *[]) {
		&setting,
		NULL,
	},
});

static const struct sm_object *const roots[] = {
	&form,
	NULL,
};

static const struct cfr_settings_policy policy[] = {
	{
		.token = TEST_TOKEN,
		.option = &setting,
		.flags = CFR_SETTINGS_POLICY_READ | CFR_SETTINGS_POLICY_WRITE |
			CFR_SETTINGS_POLICY_RUNTIME_APPLY,
	},
};

enum fake_store_state {
	STORE_ERASED,
	STORE_EMPTY,
	STORE_VALUE,
};

static struct cfr_settings_mailbox mailbox;
static enum fake_store_state store_state;
static uint32_t store_value;
static uint32_t applied_value;
static unsigned int initialize_calls;
static unsigned int write_calls;
static unsigned int apply_calls;
static unsigned int fail_write_call;
static bool fail_apply_true;

const struct cfr_settings_policy *mainboard_cfr_settings_policy(size_t *num_entries)
{
	*num_entries = ARRAY_SIZE(policy);
	return policy;
}

const struct sm_object *const *mainboard_cfr_settings_forms(size_t *num_forms)
{
	*num_forms = ARRAY_SIZE(roots) - 1;
	return roots;
}

enum cb_err mainboard_cfr_settings_validate(uint32_t token, uint32_t old_value,
					    uint32_t new_value)
{
	return token == TEST_TOKEN && old_value <= 1 && new_value <= 1 ?
		CB_SUCCESS : CB_ERR_ARG;
}

enum cb_err mainboard_cfr_settings_apply(uint32_t token, uint32_t value)
{
	apply_calls++;
	if (token != TEST_TOKEN || (fail_apply_true && value))
		return CB_ERR;
	applied_value = value;
	return CB_SUCCESS;
}

enum cb_err mainboard_cfr_settings_verify(uint32_t token, uint32_t value)
{
	return token == TEST_TOKEN && applied_value == value ? CB_SUCCESS : CB_ERR;
}

enum cb_err efi_option_get_uint(const char *name, uint32_t *value)
{
	assert_string_equal(name, "setting");
	switch (store_state) {
	case STORE_ERASED:
		return CB_EFI_FVH_INVALID;
	case STORE_EMPTY:
		return CB_EFI_OPTION_NOT_FOUND;
	case STORE_VALUE:
		*value = store_value;
		return CB_SUCCESS;
	}

	return CB_ERR;
}

enum cb_err efi_option_set_uint(const char *name, uint32_t value)
{
	assert_string_equal(name, "setting");
	write_calls++;
	if (fail_write_call == write_calls)
		return CB_EFI_ACCESS_ERROR;
	store_state = STORE_VALUE;
	store_value = value;
	return CB_SUCCESS;
}

enum cb_err efi_option_initialize_store(void)
{
	initialize_calls++;
	if (store_state != STORE_ERASED)
		return CB_EFI_FVH_INVALID;
	store_state = STORE_EMPTY;
	return CB_SUCCESS;
}

void smm_get_cfr_settings_mailbox(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&mailbox;
	*size = sizeof(mailbox);
}

bool smm_region_overlaps_handler(const struct region *r)
{
	return false;
}

static void request(uint64_t sequence, uint32_t command, uint32_t expected,
		    uint32_t value)
{
	memset(&mailbox, 0, sizeof(mailbox));
	mailbox.version = CFR_SETTINGS_VERSION;
	mailbox.size = sizeof(mailbox);
	mailbox.sequence = sequence;
	mailbox.command = command;
	mailbox.token = TEST_TOKEN;
	mailbox.expected_value = expected;
	mailbox.value = value;
	cfr_settings_smm_execute();
}

static int setup(void **state)
{
	memset(&mailbox, 0, sizeof(mailbox));
	store_state = STORE_VALUE;
	store_value = false;
	applied_value = false;
	initialize_calls = 0;
	write_calls = 0;
	apply_calls = 0;
	fail_write_call = 0;
	fail_apply_true = false;
	cfr_settings_policy_reset_for_test();
	cfr_settings_smm_reset_for_test();
	return 0;
}

static void test_initialize_erased_store_on_get(void **state)
{
	store_state = STORE_ERASED;
	request(1, CFR_SETTINGS_CMD_GET, 0, 0);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_VALUE);
	assert_int_equal(mailbox.current_value, 0);
	assert_int_equal(initialize_calls, 1);
	assert_int_equal(store_state, STORE_EMPTY);
}

static void test_apply_and_replay_once(void **state)
{
	request(1, CFR_SETTINGS_CMD_SET, 0, 1);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_APPLIED);
	assert_int_equal(store_value, 1);
	assert_int_equal(write_calls, 1);
	assert_int_equal(apply_calls, 1);

	cfr_settings_smm_execute();
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_APPLIED);
	assert_int_equal(write_calls, 1);
	assert_int_equal(apply_calls, 1);
}

static void test_sequence_wrap(void **state)
{
	request(UINT64_MAX, CFR_SETTINGS_CMD_GET, 0, 0);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_VALUE);
	request(1, CFR_SETTINGS_CMD_GET, 0, 0);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_VALUE);
}

static void test_malformed_request_does_not_advance_sequence(void **state)
{
	memset(&mailbox, 0, sizeof(mailbox));
	mailbox.size = sizeof(mailbox);
	mailbox.sequence = UINT64_MAX;
	mailbox.command = CFR_SETTINGS_CMD_GET;
	mailbox.token = TEST_TOKEN;
	cfr_settings_smm_execute();
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_INVALID_MAILBOX);

	request(1, CFR_SETTINGS_CMD_GET, 0, 0);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_VALUE);
}

static void test_roll_back_failed_apply(void **state)
{
	fail_apply_true = true;
	request(1, CFR_SETTINGS_CMD_SET, 0, 1);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_ROLLED_BACK);
	assert_int_equal(store_value, 0);
	assert_int_equal(write_calls, 2);
	assert_int_equal(apply_calls, 2);
}

static void test_latch_fault_after_failed_rollback(void **state)
{
	fail_apply_true = true;
	fail_write_call = 2;
	request(1, CFR_SETTINGS_CMD_SET, 0, 1);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_INDETERMINATE);
	assert_true(mailbox.response_flags & CFR_SETTINGS_RESP_FAULTED);

	request(2, CFR_SETTINGS_CMD_SET, 1, 0);
	assert_int_equal(mailbox.status, CFR_SETTINGS_STATUS_INDETERMINATE);
	assert_int_equal(write_calls, 2);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_initialize_erased_store_on_get, setup),
		cmocka_unit_test_setup(test_apply_and_replay_once, setup),
		cmocka_unit_test_setup(test_sequence_wrap, setup),
		cmocka_unit_test_setup(test_malformed_request_does_not_advance_sequence, setup),
		cmocka_unit_test_setup(test_roll_back_failed_apply, setup),
		cmocka_unit_test_setup(test_latch_fault_after_failed_rollback, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
