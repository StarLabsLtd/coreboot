/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <commonlib/region.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <smmstore.h>

static const struct region smmstore_region = {
	.offset = 0x180000,
	.size = 0x20000,
};
static int fmap_result;
static int ro_result;
static unsigned int fmap_calls;
static unsigned int ro_calls;

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

int printk(int msg_level, const char *fmt, ...)
{
	(void)msg_level;
	(void)fmt;
	return 0;
}

int fmap_locate_area(const char *name, struct region *region)
{
	fmap_calls++;
	assert(strcmp(name, "SMMSTORE") == 0);
	if (fmap_result)
		return fmap_result;

	*region = smmstore_region;
	return 0;
}

int boot_device_ro_subregion(const struct region *region,
	struct region_device *output)
{
	ro_calls++;
	assert(region->offset == smmstore_region.offset);
	assert(region->size == smmstore_region.size);
	memset(output, 0x5a, sizeof(*output));
	return ro_result;
}

static void reset_test(void)
{
	fmap_result = 0;
	ro_result = 0;
	fmap_calls = 0;
	ro_calls = 0;
}

static void assert_filled(const struct region_device *output, uint8_t value)
{
	const uint8_t *bytes = (const uint8_t *)output;

	for (size_t i = 0; i < sizeof(*output); i++)
		assert(bytes[i] == value);
}

static void test_success(void)
{
	struct region_device output;

	memset(&output, 0xa5, sizeof(output));
	reset_test();
	assert(smmstore_lookup_read_region(&output) == 0);
	assert(fmap_calls == 1);
	assert(ro_calls == 1);
	assert_filled(&output, 0x5a);
}

static void test_full_flash_mode_is_ignored(void)
{
	struct region_device output;
	uint8_t command = SMMSTORE_CMD_USE_FULL_FLASH;

	assert(smmstore_preprocess_cmd(&command, (void *)(uintptr_t)1) == 1);
	command = SMMSTORE_CMD_RAW_READ | SMMSTORE_CMD_USE_FULL_FLASH;
	assert(smmstore_preprocess_cmd(&command, (void *)(uintptr_t)1) == 0);
	assert(command == SMMSTORE_CMD_RAW_READ);

	memset(&output, 0xa5, sizeof(output));
	reset_test();
	assert(smmstore_lookup_read_region(&output) == 0);
	assert(fmap_calls == 1);
	assert(ro_calls == 1);
	assert_filled(&output, 0x5a);
}

static void test_null_output(void)
{
	reset_test();
	assert(smmstore_lookup_read_region(NULL) == -1);
	assert(fmap_calls == 0);
	assert(ro_calls == 0);
}

static void test_fmap_failure(void)
{
	struct region_device output;

	memset(&output, 0xa5, sizeof(output));
	reset_test();
	fmap_result = -1;
	assert(smmstore_lookup_read_region(&output) == -1);
	assert(fmap_calls == 1);
	assert(ro_calls == 0);
	assert_filled(&output, 0xa5);
}

static void test_read_subregion_failure(void)
{
	struct region_device output;

	memset(&output, 0xa5, sizeof(output));
	reset_test();
	ro_result = -1;
	assert(smmstore_lookup_read_region(&output) == -1);
	assert(fmap_calls == 1);
	assert(ro_calls == 1);
	assert_filled(&output, 0xa5);
}

int main(void)
{
	test_success();
	test_full_flash_mode_is_ignored();
	test_null_output();
	test_fmap_failure();
	test_read_subregion_failure();
	return 0;
}
