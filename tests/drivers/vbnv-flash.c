/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>
#include <tests/test.h>
#include <vb2_api.h>
#include <2nvstorage.h>
#include <2nvstorage_fields.h>

/* Include the production backend so reboot tests can reset its private RAM state. */
#include "../../src/security/vboot/vbnv_flash_redundant.c"

static uint8_t flash[0x2000], saved[0x2000];
static struct region_device root;
static unsigned int operations, fail_at;
static size_t partial;
static bool lost_power;

static ssize_t read_flash(const struct region_device *dev, void *data, size_t offset, size_t size)
{
	assert_true(offset + size <= sizeof(flash));
	if (lost_power)
		return -1;
	memcpy(data, flash + offset, size);
	return size;
}

static ssize_t write_flash(const struct region_device *dev, const void *data,
			   size_t offset, size_t size)
{
	const uint8_t *bytes = data;
	size_t count = size;

	assert_true(offset + size <= sizeof(flash));
	if (lost_power)
		return -1;
	if (++operations == fail_at) {
		count = MIN(partial, size);
		lost_power = true;
	}
	for (size_t i = 0; i < count; i++) {
		assert_int_equal(flash[offset + i] & bytes[i], bytes[i]);
		flash[offset + i] &= bytes[i];
	}
	return lost_power ? -1 : size;
}

static ssize_t erase_flash(const struct region_device *dev, size_t offset, size_t size)
{
	size_t count = size;

	assert_true(offset + size <= sizeof(flash));
	if (lost_power)
		return -1;
	if (++operations == fail_at) {
		count = MIN(partial, size);
		lost_power = true;
	}
	memset(flash + offset, 0xff, count);
	return lost_power ? -1 : size;
}

static const struct region_device_ops ops = {
	.readat = read_flash,
	.writeat = write_flash,
	.eraseat = erase_flash,
};

int fmap_locate_area_as_rdev_rw(const char *name, struct region_device *dev)
{
	assert_string_equal(name, "RW_NVRAM");
	return rdev_chain_full(dev, &root);
}

void vb2ex_printf(const char *function, const char *format, ...) { }

static void reboot(void)
{
	memset(&vbnv_flash, 0, sizeof(vbnv_flash));
	operations = fail_at = 0;
	lost_power = false;
}

static void data_for(unsigned int value, uint8_t data[VB2_NVDATA_SIZE])
{
	struct vb2_context ctx = { 0 };

	ctx.nvdata[0] = VB2_NV_HEADER_SIGNATURE_V1;
	vb2_nv_set(&ctx, VB2_NV_FIRMWARE_SETTINGS_RESET, 1);
	vb2_nv_set(&ctx, VB2_NV_LOCALIZATION_INDEX, value);
	assert_int_equal(vb2_nv_check_crc(&ctx), VB2_SUCCESS);
	memcpy(data, ctx.nvdata, VB2_NVDATA_SIZE);
}

static int setup(void **state)
{
	root = (struct region_device) {
		.ops = &ops, .region = { .offset = 0, .size = sizeof(flash) },
	};
	memset(flash, 0xff, sizeof(flash));
	reboot();
	return 0;
}

static void test_torn_append(void **state)
{
	uint8_t old[VB2_NVDATA_SIZE], next[VB2_NVDATA_SIZE], actual[VB2_NVDATA_SIZE];

	data_for(0, old);
	data_for(200, next);
	assert_int_equal(save_vbnv_flash(old), 0);
	operations = 0;
	fail_at = 1;
	partial = 1;
	assert_int_equal(save_vbnv_flash(next), -1);
	reboot();
	read_vbnv_flash(actual);
	assert_memory_equal(actual, old, sizeof(old));
}

static void test_append_and_reboot(void **state)
{
	uint8_t data[VB2_NVDATA_SIZE], actual[VB2_NVDATA_SIZE];

	for (unsigned int i = 0; i < 400; i++) {
		data_for(i & 255, data);
		assert_int_equal(save_vbnv_flash(data), 0);
		reboot();
		read_vbnv_flash(actual);
		assert_memory_equal(actual, data, sizeof(data));
	}
	assert_int_equal(vbnv_flash.sequence, 400);
}

static void test_power_cuts(void **state)
{
	uint8_t old[VB2_NVDATA_SIZE], next[VB2_NVDATA_SIZE], actual[VB2_NVDATA_SIZE];

	for (unsigned int rollover = 0; rollover < 3; rollover++) {
		memset(flash, 0xff, sizeof(flash));
		reboot();
		unsigned int erase = rollover ? 1 : 0;
		unsigned int count = rollover ? rollover * BANK_SIZE / sizeof(struct vbnv_record) : 1;
		for (unsigned int i = 0; i < count; i++) {
			data_for(i & 255, old);
			assert_int_equal(save_vbnv_flash(old), 0);
		}
		data_for(200, next);
		memcpy(saved, flash, sizeof(saved));
		for (unsigned int operation = 1; operation <= 2 + erase; operation++) {
			size_t length = erase && operation == 1 ? BANK_SIZE :
				operation == 1 + erase ? offsetof(struct vbnv_record, committed) : 4;
			for (size_t prefix = 0; prefix <= length; prefix++) {
				memcpy(flash, saved, sizeof(flash));
				reboot();
				fail_at = operation;
				partial = prefix;
				assert_int_equal(save_vbnv_flash(next), -1);
				reboot();
				read_vbnv_flash(actual);
				assert_true(!memcmp(actual, old, sizeof(old)) ||
					    !memcmp(actual, next, sizeof(next)));
				assert_int_equal(save_vbnv_flash(next), 0);
				reboot();
				read_vbnv_flash(actual);
				assert_memory_equal(actual, next, sizeof(next));
			}
		}
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_torn_append, setup),
		cmocka_unit_test_setup(test_append_and_reboot, setup),
		cmocka_unit_test_setup(test_power_cuts, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
