/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <drivers/efi/efivars.h>
#include <drivers/efi/option.h>
#include <option.h>
#include <smmstore.h>
#include <tests/test.h>

static uint64_t com_buffer;
static uint64_t generation;
static bool generation_available = true;

uint64_t *smm_get_smmstore_generation(void)
{
	return generation_available ? &generation : NULL;
}
static unsigned int writes;
static enum cb_err write_result;
static unsigned int raw_writes;
static bool full_flash_enabled;

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
	/* Model the update-boot gate for the MM dispatch test. */
	if (CONFIG(PAYLOAD_MM_INTERFACE))
		*cmd &= ~SMMSTORE_CMD_USE_FULL_FLASH;
	return 0;
}

bool smmstore_full_flash_enabled(void)
{
	return full_flash_enabled;
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
	raw_writes++;
	return -1;
}

int smmstore_rawclear_region(uint32_t block_id)
{
	raw_writes++;
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
	if (CONFIG(PAYLOAD_MM_INTERFACE)) {
		struct smmstore_params_raw_write params = { 0 };

		assert_int_equal(set_uint_option("trackpad_state", 22), CB_ERR);
		assert_int_equal(efi_option_set_uint("trackpad_state", 22), CB_ERR);
		assert_int_equal(writes, 0);
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &params),
				 SMMSTORE_RET_UNSUPPORTED);
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_END, &params),
				 SMMSTORE_RET_UNSUPPORTED);
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_WRITE, &params),
				 SMMSTORE_RET_UNSUPPORTED);
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_CLEAR, &params),
				 SMMSTORE_RET_UNSUPPORTED);
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_READ, &params),
				 SMMSTORE_RET_UNSUPPORTED);
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH |
			SMMSTORE_CMD_RAW_WRITE, &params), SMMSTORE_RET_UNSUPPORTED);
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH | 0x7f, &params),
				 SMMSTORE_RET_UNSUPPORTED);
		assert_int_equal(raw_writes, 0);

		full_flash_enabled = true;
		assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH |
			SMMSTORE_CMD_RAW_WRITE, &params), SMMSTORE_RET_FAILURE);
		assert_int_equal(raw_writes, 1);
		return;
	}

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

	/* A retained counter must be read from shared state, not an SMM copy. */
	generation = 8;
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &unused),
			 SMMSTORE_RET_SUCCESS);
	assert_int_equal(com_buffer, 8);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_END, &unused),
			 SMMSTORE_RET_SUCCESS);

	generation_available = false;
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &unused),
			 SMMSTORE_RET_FAILURE);
	assert_int_equal(set_uint_option("trackpad_state", 22), CB_ERR);
	assert_int_equal(writes, 1);
	generation_available = true;

	/* A partially failed append also invalidates the payload cache. */
	write_result = CB_EFI_ACCESS_ERROR;
	assert_int_equal(set_uint_option("trackpad_state", 0), CB_EFI_ACCESS_ERROR);
	assert_int_equal(writes, 2);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_VARIABLE_BEGIN, &unused),
			 SMMSTORE_RET_SUCCESS);
	assert_int_equal(com_buffer, 9);
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
