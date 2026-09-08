/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <drivers/efi/efivars.h>
#include <option.h>
#include <smmstore.h>
#include <tests/test.h>

static uint64_t com_buffer;
static unsigned int writes;
static enum cb_err write_result;

void smm_get_smmstore_com_buffer(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&com_buffer;
	*size = sizeof(com_buffer);
}

int smmstore_init(void *buf, size_t len)
{
	assert_ptr_equal(buf, &com_buffer);
	assert_int_equal(len, sizeof(com_buffer));
	return 0;
}

int smmstore_preprocess_cmd(uint8_t *cmd, void *param)
{
	return 0;
}

bool smm_region_overlaps_handler(const struct region *r)
{
	return false;
}

int smmstore_rawread_region(uint32_t block_id, uint32_t offset, uint32_t bufsize)
{
	return -1;
}

int smmstore_rawwrite_region(uint32_t block_id, uint32_t offset, uint32_t bufsize)
{
	return -1;
}

int smmstore_rawclear_region(uint32_t block_id)
{
	return -1;
}

int smmstore_lookup_region(struct region_device *rstore)
{
	return 0;
}

enum cb_err efi_fv_set_option(const struct region_device *rdev, const EFI_GUID *guid,
			      const char *name, void *data, uint32_t size)
{
	writes++;
	return write_result;
}

static void variable_operation(void **state)
{
	uint32_t unused = 0;

	if (!CONFIG(SMMSTORE)) {
		write_result = CB_SUCCESS;
		assert_int_equal(set_uint_option("trackpad_state", 22), CB_SUCCESS);
		assert_int_equal(writes, 1);
		return;
	}

	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_END, &unused),
			 SMMSTORE_RET_FAILURE);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &unused),
			 SMMSTORE_RET_SUCCESS);
	assert_true(smmstore_variable_busy());
	assert_int_equal(com_buffer, 0);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &unused),
			 SMMSTORE_RET_BUSY);
	assert_int_equal(set_uint_option("trackpad_state", 22), CB_ERR);
	assert_int_equal(writes, 0);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_END, &unused),
			 SMMSTORE_RET_SUCCESS);
	assert_false(smmstore_variable_busy());

	write_result = CB_SUCCESS;
	assert_int_equal(set_uint_option("trackpad_state", 22), CB_SUCCESS);
	assert_int_equal(writes, 1);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &unused),
			 SMMSTORE_RET_SUCCESS);
	assert_int_equal(com_buffer, 1);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_END, &unused),
			 SMMSTORE_RET_SUCCESS);

	/* A partially failed append also invalidates the payload cache. */
	write_result = CB_EFI_ACCESS_ERROR;
	assert_int_equal(set_uint_option("trackpad_state", 0), CB_EFI_ACCESS_ERROR);
	assert_int_equal(writes, 2);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &unused),
			 SMMSTORE_RET_SUCCESS);
	assert_int_equal(com_buffer, 2);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_END, &unused),
			 SMMSTORE_RET_SUCCESS);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(variable_operation),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
