/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi_gnvs.h>
#include <commonlib/helpers.h>
#include <common/touchpad.h>
#include <cpu/x86/smm.h>
#include <drivers/efi/option.h>
#include <drivers/i2c/designware/dw_i2c.h>
#include <drivers/option/cfr_runtime.h>
#include <option.h>
#include <soc/nvs.h>
#include <starlabs/efi_option_smi.h>
#include <string.h>
#include <tests/test.h>

#define TOUCHPAD_OPTION_COUNT (CONFIG(STARLABS_TOUCHPAD_PIXART) ? 4 : 3)

static uint8_t gnvs_storage[ALIGN_UP(sizeof(struct global_nvs), sizeof(uint64_t)) +
			    sizeof(struct starlabs_dnvs_efiopt)];
struct global_nvs *gnvs = (void *)gnvs_storage;

static enum cb_err persistence_result;
static int persistence_calls;
static const char *persisted_name;
static unsigned int persisted_value;
static bool mutate_request_during_persistence;
static enum cb_err scalar_result;
static unsigned int scalar_value;
static int scalar_reads;
static bool mutate_request_during_read;
static bool lock_available;
static int ec_apply_calls;
static int ec_write_result;
static enum cb_err batch_result;
static enum cb_err batch_option_results[4];
static uint32_t batch_values[4];
static int batch_reads;
static bool touchpad_available;
static int availability_checks;
static enum cb_err touchpad_apply_result;
static int touchpad_apply_calls;
static uint8_t applied_haptics;
static uint16_t applied_press;
static uint16_t applied_release;
static uint8_t applied_rate;

int mainboard_smi_apmc(u8 data);

static struct starlabs_dnvs_efiopt *mailbox(void)
{
	return (void *)(gnvs_storage + ALIGN_UP(sizeof(struct global_nvs), sizeof(uint64_t)));
}

enum cb_err set_uint_option(const char *name, unsigned int value)
{
	persistence_calls++;
	persisted_name = name;
	persisted_value = value;
	/* Caller-writable request/reply fields must not affect captured state. */
	if (mutate_request_during_persistence) {
		mailbox()->cmd = STARLABS_EFIOPT_CMD_GET;
		mailbox()->id = UINT32_MAX;
		mailbox()->value = UINT32_MAX;
		mailbox()->status = CB_SUCCESS;
	}
	return persistence_result;
}

enum cb_err get_uint_option_status(const char *name, unsigned int fallback,
				   unsigned int *value)
{
	scalar_reads++;
	*value = scalar_value;
	if (mutate_request_during_read) {
		mailbox()->cmd = STARLABS_EFIOPT_CMD_SET;
		mailbox()->id = UINT32_MAX;
		mailbox()->value = UINT32_MAX;
		mailbox()->status = CB_SUCCESS;
	}
	return scalar_result;
}

bool smm_acpi_global_lock_acquire(void)
{
	ec_apply_calls++;
	return lock_available;
}

bool smm_acpi_global_lock_release(void)
{
	return true;
}

int send_ec_command(u8 command)
{
	return ec_write_result;
}

int send_ec_data(u8 data)
{
	return ec_write_result;
}

void pmc_enable_smi(uint32_t mask)
{
}

enum cb_err efi_option_get_uints(struct efi_uint_option *options, size_t count)
{
	assert_int_equal(count, TOUCHPAD_OPTION_COUNT);
	batch_reads++;
	if (batch_result != CB_SUCCESS)
		return batch_result;

	for (size_t i = 0; i < count; i++) {
		options[i].value = batch_values[i];
		options[i].result = batch_option_results[i];
	}
	return CB_SUCCESS;
}

bool starlabs_touchpad_runtime_available(void)
{
	availability_checks++;
	return touchpad_available;
}

enum cb_err starlabs_touchpad_runtime_apply(uint8_t haptics, uint16_t press,
					    uint16_t release, uint8_t rate)
{
	touchpad_apply_calls++;
	applied_haptics = haptics;
	applied_press = press;
	applied_release = release;
	applied_rate = rate;
	return touchpad_apply_result;
}

enum cb_err dw_i2c_smm_init(unsigned int bus)
{
	return CB_SUCCESS;
}

static void set_valid_batch_values(void)
{
	batch_values[0] = STARLABS_TOUCHPAD_HAPTICS_HIGH;
	batch_values[1] = STARLABS_TOUCHPAD_PRESS_FORCE_HIGH;
	batch_values[2] = STARLABS_TOUCHPAD_RELEASE_FORCE_LOW;
#if CONFIG(STARLABS_TOUCHPAD_PIXART)
	batch_values[3] = STARLABS_TOUCHPAD_RATE_FAST;
#endif
}

static int setup(void **state)
{
	memset(gnvs_storage, 0, sizeof(gnvs_storage));
	persistence_result = CB_SUCCESS;
	persistence_calls = 0;
	persisted_name = NULL;
	persisted_value = 0;
	mutate_request_during_persistence = false;
	scalar_result = CB_EFI_OPTION_NOT_FOUND;
	scalar_value = 0;
	scalar_reads = 0;
	mutate_request_during_read = false;
	lock_available = false;
	ec_apply_calls = 0;
	ec_write_result = 0;
	batch_result = CB_SUCCESS;
	for (size_t i = 0; i < ARRAY_SIZE(batch_option_results); i++)
		batch_option_results[i] = CB_SUCCESS;
	set_valid_batch_values();
	batch_reads = 0;
	touchpad_available = true;
	availability_checks = 0;
	touchpad_apply_result = CB_SUCCESS;
	touchpad_apply_calls = 0;
	applied_haptics = 0;
	applied_press = 0;
	applied_release = 0;
	applied_rate = 0;
	return 0;
}

static void invalid_request_cannot_be_enabled_by_reply_mutation(void **state)
{
	struct starlabs_dnvs_efiopt *dnvs = mailbox();

	dnvs->cmd = STARLABS_EFIOPT_CMD_SET;
	dnvs->id = STARLABS_EFIOPT_ID_FN_LOCK_STATE;
	dnvs->value = 0xdeadbeef;
	dnvs->status = CB_SUCCESS;

	assert_int_equal(mainboard_smi_apmc(STARLABS_APMC_CMD_EFI_OPTION), 1);
	assert_int_equal((int32_t)dnvs->status, CB_ERR_ARG);
	assert_int_equal(persistence_calls, 0);
	assert_int_equal(ec_apply_calls, 0);
}

static void persistence_failure_is_reported_without_application(void **state)
{
	struct starlabs_dnvs_efiopt *dnvs = mailbox();

	persistence_result = CB_ERR;
	mutate_request_during_persistence = true;
	dnvs->cmd = STARLABS_EFIOPT_CMD_SET;
	dnvs->id = STARLABS_EFIOPT_ID_FN_LOCK_STATE;
	dnvs->value = 1;

	assert_int_equal(mainboard_smi_apmc(STARLABS_APMC_CMD_EFI_OPTION), 1);
	assert_int_equal((int32_t)dnvs->status, CB_ERR);
	assert_int_equal(dnvs->value, 1);
	assert_int_equal(persistence_calls, 1);
	assert_string_equal(persisted_name, "fn_lock_state");
	assert_int_equal(persisted_value, 1);
	assert_int_equal(ec_apply_calls, 0);
}

static void successful_set_only_persists_captured_request(void **state)
{
	struct starlabs_dnvs_efiopt *dnvs = mailbox();

	mutate_request_during_persistence = true;
	dnvs->cmd = STARLABS_EFIOPT_CMD_SET;
	dnvs->id = STARLABS_EFIOPT_ID_FN_LOCK_STATE;
	dnvs->value = 1;

	assert_int_equal(mainboard_smi_apmc(STARLABS_APMC_CMD_EFI_OPTION), 1);
	assert_int_equal((int32_t)dnvs->status, CB_SUCCESS);
	assert_int_equal(dnvs->value, 1);
	assert_int_equal(persistence_calls, 1);
	assert_string_equal(persisted_name, "fn_lock_state");
	assert_int_equal(persisted_value, 1);
	assert_int_equal(ec_apply_calls, 0);
	assert_int_equal(touchpad_apply_calls, 0);
}

static void get_uses_captured_request_and_initializes_reply(void **state)
{
	struct starlabs_dnvs_efiopt *dnvs = mailbox();

	scalar_result = CB_SUCCESS;
	scalar_value = 1;
	mutate_request_during_read = true;
	dnvs->cmd = STARLABS_EFIOPT_CMD_GET;
	dnvs->id = STARLABS_EFIOPT_ID_FN_LOCK_STATE;
	dnvs->value = 0xa5a5a5a5;

	assert_int_equal(mainboard_smi_apmc(STARLABS_APMC_CMD_EFI_OPTION), 1);
	assert_int_equal((int32_t)dnvs->status, CB_SUCCESS);
	assert_int_equal(dnvs->value, 1);
	assert_int_equal(scalar_reads, 1);

	scalar_result = CB_EFI_ACCESS_ERROR;
	dnvs->cmd = STARLABS_EFIOPT_CMD_GET;
	dnvs->id = STARLABS_EFIOPT_ID_FN_LOCK_STATE;
	dnvs->value = 0x5a5a5a5a;
	assert_int_equal(mainboard_smi_apmc(STARLABS_APMC_CMD_EFI_OPTION), 1);
	assert_int_equal((int32_t)dnvs->status, CB_EFI_ACCESS_ERROR);
	assert_int_equal(dnvs->value, 0x5a5a5a5a);
}

static void explicit_ec_application_reports_refusal_and_failure(void **state)
{
	scalar_result = CB_SUCCESS;
	scalar_value = 1;
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_FN_LOCK_STATE), CB_ERR);
	assert_int_equal(persistence_calls, 0);

	lock_available = true;
	ec_write_result = -1;
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_FN_LOCK_STATE), CB_ERR);
	assert_int_equal(persistence_calls, 0);
}

static void assert_grouped_values(void)
{
	assert_int_equal(applied_haptics, batch_values[0]);
	assert_int_equal(applied_press, batch_values[1]);
	assert_int_equal(applied_release, batch_values[2]);
#if CONFIG(STARLABS_TOUCHPAD_PIXART)
	assert_int_equal(applied_rate, batch_values[3]);
#else
	assert_int_equal(applied_rate, STARLABS_TOUCHPAD_REPORT_RATE_DEFAULT);
#endif
}

static void every_grouped_token_uses_one_batch_and_no_scalar_read(void **state)
{
	const enum starlabs_efiopt_id ids[] = {
		STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS,
		STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_PRESS,
		STARLABS_EFIOPT_ID_TOUCHPAD_FORCE_RELEASE,
#if CONFIG(STARLABS_TOUCHPAD_PIXART)
		STARLABS_EFIOPT_ID_TOUCHPAD_REPORT_RATE,
#endif
	};

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		assert_int_equal(cfr_runtime_apply_option(ids[i]), CB_SUCCESS);
		assert_int_equal(batch_reads, i + 1);
		assert_int_equal(touchpad_apply_calls, i + 1);
		assert_int_equal(scalar_reads, 0);
		assert_grouped_values();
	}
}

static void missing_grouped_values_use_valid_defaults(void **state)
{
	for (size_t i = 0; i < TOUCHPAD_OPTION_COUNT; i++)
		batch_option_results[i] = CB_EFI_OPTION_NOT_FOUND;

	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS),
			 CB_SUCCESS);
	assert_int_equal(applied_haptics, STARLABS_TOUCHPAD_HAPTICS_DEFAULT);
	assert_int_equal(applied_press, STARLABS_TOUCHPAD_PRESS_FORCE_DEFAULT);
	assert_int_equal(applied_release, STARLABS_TOUCHPAD_RELEASE_FORCE_DEFAULT);
	assert_int_equal(applied_rate, STARLABS_TOUCHPAD_REPORT_RATE_DEFAULT);
}

static void invalid_grouped_inputs_do_not_reach_hardware(void **state)
{
	batch_option_results[1] = CB_EFI_ACCESS_ERROR;
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS),
			 CB_EFI_ACCESS_ERROR);
	assert_int_equal(touchpad_apply_calls, 0);

	setup(state);
	batch_values[0] = UINT32_MAX;
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS),
			 CB_ERR_ARG);
	assert_int_equal(touchpad_apply_calls, 0);

	setup(state);
	batch_result = CB_ERR;
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS), CB_ERR);
	assert_int_equal(touchpad_apply_calls, 0);
}

static void unavailable_runtime_is_rejected_before_store_work(void **state)
{
	touchpad_available = false;
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS), CB_ERR);
	assert_int_equal(availability_checks, 1);
	assert_int_equal(batch_reads, 0);
	assert_int_equal(touchpad_apply_calls, 0);
}

static void grouped_application_failure_is_unambiguous(void **state)
{
	touchpad_apply_result = CB_ERR;
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS), CB_ERR);
	assert_int_equal(batch_reads, 1);
	assert_int_equal(touchpad_apply_calls, 1);
	assert_int_equal(persistence_calls, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(invalid_request_cannot_be_enabled_by_reply_mutation, setup),
		cmocka_unit_test_setup(persistence_failure_is_reported_without_application,
				       setup),
		cmocka_unit_test_setup(successful_set_only_persists_captured_request, setup),
		cmocka_unit_test_setup(get_uses_captured_request_and_initializes_reply, setup),
		cmocka_unit_test_setup(explicit_ec_application_reports_refusal_and_failure,
				       setup),
		cmocka_unit_test_setup(every_grouped_token_uses_one_batch_and_no_scalar_read,
				       setup),
		cmocka_unit_test_setup(missing_grouped_values_use_valid_defaults, setup),
		cmocka_unit_test_setup(invalid_grouped_inputs_do_not_reach_hardware, setup),
		cmocka_unit_test_setup(unavailable_runtime_is_rejected_before_store_work, setup),
		cmocka_unit_test_setup(grouped_application_failure_is_unambiguous, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
