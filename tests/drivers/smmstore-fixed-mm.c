/* SPDX-License-Identifier: GPL-2.0-only */
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <smmstore.h>
#include <tests/test.h>

int smmstore_preprocess_cmd(uint8_t *command, void *param)
{
	fail_msg("Raw mutation reached preprocessing");
	return 0;
}
int smmstore_init(void *buffer, size_t size)
{
	fail_msg("Raw mutation reached hardware initialization");
	return -1;
}
void smm_get_smmstore_com_buffer(uintptr_t *base, size_t *size)
{
	fail_msg("Raw mutation requested a hardware buffer");
}
bool smm_region_overlaps_handler(const struct region *region)
{
	return false;
}
int smmstore_rawread_region(uint32_t block, uint32_t offset, uint32_t size)
{
	fail_msg("Unexpected raw read");
	return -1;
}
int smmstore_rawwrite_region(uint32_t block, uint32_t offset, uint32_t size)
{
	fail_msg("Raw mutation reached the flash writer");
	return -1;
}
int smmstore_rawclear_region(uint32_t block)
{
	fail_msg("Raw mutation reached the flash eraser");
	return -1;
}
static void raw_mutations_denied(void **state)
{
	struct smmstore_params_raw_write write = { .bufsize = 1 };
	struct smmstore_params_raw_clear clear = { .block_id = 0 };

	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH, (void *)1),
			 SMMSTORE_RET_UNSUPPORTED);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH, NULL),
			 SMMSTORE_RET_SUCCESS);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_WRITE, &write), SMMSTORE_RET_FAILURE);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_RAW_CLEAR, &clear), SMMSTORE_RET_FAILURE);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH | SMMSTORE_CMD_RAW_WRITE,
				      &write), SMMSTORE_RET_UNSUPPORTED);
	assert_int_equal(smmstore_exec(SMMSTORE_CMD_USE_FULL_FLASH | SMMSTORE_CMD_RAW_CLEAR,
				      &clear), SMMSTORE_RET_UNSUPPORTED);
}
int main(void)
{
	const struct CMUnitTest tests[] = { cmocka_unit_test(raw_mutations_denied) };
	return cb_run_group_tests(tests, NULL, NULL);
}
