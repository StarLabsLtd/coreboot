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

static uint8_t gnvs_storage[ALIGN_UP(sizeof(struct global_nvs), sizeof(uint64_t)) +
			    sizeof(struct starlabs_dnvs_efiopt)];
struct global_nvs *gnvs = (void *)gnvs_storage;

static enum cb_err persistence_result;
static int persistence_calls;
static int ec_apply_calls;
static int batch_reads;
static int touchpad_apply_calls;

int mainboard_smi_apmc(u8 data);

static struct starlabs_dnvs_efiopt *mailbox(void)
{
	return (void *)(gnvs_storage + ALIGN_UP(sizeof(struct global_nvs), sizeof(uint64_t)));
}

enum cb_err set_uint_option(const char *name, unsigned int value)
{
	persistence_calls++;
	/* Simulate a caller changing a published reply while SMM is processing. */
	mailbox()->status = CB_SUCCESS;
	return persistence_result;
}

enum cb_err get_uint_option_status(const char *name, unsigned int fallback,
				   unsigned int *value)
{
	return CB_EFI_OPTION_NOT_FOUND;
}

bool smm_acpi_global_lock_acquire(void)
{
	ec_apply_calls++;
	return false;
}

bool smm_acpi_global_lock_release(void)
{
	return true;
}

int send_ec_command(u8 command)
{
	return 0;
}

int send_ec_data(u8 data)
{
	return 0;
}

void pmc_enable_smi(uint32_t mask)
{
}

enum cb_err efi_option_get_uints(struct efi_uint_option *options, size_t count)
{
	const uint32_t values[] = {
		STARLABS_TOUCHPAD_HAPTICS_DEFAULT,
		STARLABS_TOUCHPAD_PRESS_FORCE_DEFAULT,
		STARLABS_TOUCHPAD_RELEASE_FORCE_DEFAULT,
		STARLABS_TOUCHPAD_REPORT_RATE_DEFAULT,
	};

	assert_int_equal(count, ARRAY_SIZE(values));
	batch_reads++;
	for (size_t i = 0; i < count; i++) {
		options[i].value = values[i];
		options[i].result = CB_SUCCESS;
	}
	return CB_SUCCESS;
}

enum cb_err starlabs_touchpad_runtime_apply(uint8_t haptics, uint16_t press,
					    uint16_t release, uint8_t rate)
{
	touchpad_apply_calls++;
	return CB_SUCCESS;
}

enum cb_err dw_i2c_smm_init(unsigned int bus)
{
	return CB_SUCCESS;
}

static int setup(void **state)
{
	memset(gnvs_storage, 0, sizeof(gnvs_storage));
	persistence_result = CB_SUCCESS;
	persistence_calls = 0;
	ec_apply_calls = 0;
	batch_reads = 0;
	touchpad_apply_calls = 0;
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

static void persistence_failure_cannot_be_enabled_by_reply_mutation(void **state)
{
	struct starlabs_dnvs_efiopt *dnvs = mailbox();

	persistence_result = CB_ERR;
	dnvs->cmd = STARLABS_EFIOPT_CMD_SET;
	dnvs->id = STARLABS_EFIOPT_ID_FN_LOCK_STATE;
	dnvs->value = 1;
	dnvs->status = CB_ERR;

	assert_int_equal(mainboard_smi_apmc(STARLABS_APMC_CMD_EFI_OPTION), 1);
	assert_int_equal((int32_t)dnvs->status, CB_ERR);
	assert_int_equal(persistence_calls, 1);
	assert_int_equal(ec_apply_calls, 0);
}

static void persisted_but_refused_application_reports_failure(void **state)
{
	struct starlabs_dnvs_efiopt *dnvs = mailbox();

	dnvs->cmd = STARLABS_EFIOPT_CMD_SET;
	dnvs->id = STARLABS_EFIOPT_ID_FN_LOCK_STATE;
	dnvs->value = 1;

	assert_int_equal(mainboard_smi_apmc(STARLABS_APMC_CMD_EFI_OPTION), 1);
	assert_int_equal((int32_t)dnvs->status, CB_ERR);
	assert_int_equal(persistence_calls, 1);
	assert_int_equal(ec_apply_calls, 1);
}

static void grouped_apply_uses_one_store_collection(void **state)
{
	assert_int_equal(cfr_runtime_apply_option(STARLABS_EFIOPT_ID_TOUCHPAD_HAPTICS),
			 CB_SUCCESS);
	assert_int_equal(batch_reads, 1);
	assert_int_equal(touchpad_apply_calls, 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(invalid_request_cannot_be_enabled_by_reply_mutation, setup),
		cmocka_unit_test_setup(persistence_failure_cannot_be_enabled_by_reply_mutation,
				       setup),
		cmocka_unit_test_setup(persisted_but_refused_application_reports_failure, setup),
		cmocka_unit_test_setup(grouped_apply_uses_one_store_collection, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
