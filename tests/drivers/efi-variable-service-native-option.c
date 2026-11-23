/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/efi/option.h>
#include <option.h>
#include <smmstore.h>
#include <tests/test.h>

#include <drivers/efi_variable_service/smi.c>

_Static_assert(sizeof(EFI_STATUS) == 8, "Test requires X64 EFI status");

static uint32_t reply_status;
static uint32_t reply_size;
static struct efi_variable_service_state test_service;

bool ass_native_smm_boot_access(void *arena, bool *allowed)
{
	*allowed = true;
	return true;
}

bool ass_native_smm_variable(void *arena, void *message, size_t size)
{
	struct smm_variable_header32 *variable = message;
	struct smm_variable_access32 *access = (void *)variable->data;

	assert_int_equal(variable->function, SMM_VARIABLE_FUNCTION_GET_VARIABLE);
	assert_int_equal(access->data_size, sizeof(uint32_t));
	variable->return_status = reply_status;
	access->data_size = reply_size;
	access->attributes = EFI_VARIABLE_NON_VOLATILE;
	return true;
}

int smmstore_lookup_region(struct region_device *store)
{
	fail();
	return -1;
}

enum cb_err efi_fv_get_option(const struct region_device *store, const EFI_GUID *guid,
			     const char *name, void *data, uint32_t *size)
{
	fail();
	return CB_EFI_ACCESS_ERROR;
}

static int setup(void **unused)
{
	current_state = SERVICE_READY;
	service = &test_service;
	return 0;
}

static void absent_option_returns_default(void **unused)
{
	unsigned int value = 0;

	reply_status = 0x8000000eU; /* IA32 wire EFI_NOT_FOUND */
	reply_size = sizeof(value);
	assert_int_equal(get_uint_option_status("rom_armor", 1, &value),
			 CB_EFI_OPTION_NOT_FOUND);
	assert_int_equal(value, 1);
}

static void larger_option_reports_size_error(void **unused)
{
	uint32_t value = 0xfeedbeef;

	reply_status = 0x80000005U; /* IA32 wire EFI_BUFFER_TOO_SMALL */
	reply_size = sizeof(uint64_t);
	assert_int_equal(efi_option_get_uint("rom_armor", &value), CB_EFI_BUFFER_TOO_SMALL);
	assert_int_equal(value, 0xfeedbeef);
	reply_size = sizeof(value);
	assert_int_equal(efi_option_get_uint("rom_armor", &value), CB_EFI_ACCESS_ERROR);
	assert_int_equal(value, 0xfeedbeef);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(absent_option_returns_default, setup),
		cmocka_unit_test_setup(larger_option_reports_size_error, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
