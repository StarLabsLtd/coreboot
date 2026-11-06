/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <boot/coreboot_tables.h>
#include <framebuffer_info.h>
#include <fsp/graphics.h>
#include <string.h>

static struct hob_graphics_info graphics;
static struct lb_framebuffer reported;
static size_t hob_size;
static unsigned int reports;

const void *fsp_find_extension_hob_by_guid(const uint8_t *guid, size_t *size)
{
	assert_memory_equal(guid, fsp_graphics_info_guid, sizeof(fsp_graphics_info_guid));
	*size = hob_size;
	return &graphics;
}

struct fb_info *fb_add_framebuffer_info_ex(const struct lb_framebuffer *fb)
{
	reported = *fb;
	reports++;
	return NULL;
}

static int setup(void **state)
{
	graphics = (struct hob_graphics_info) {
		.framebuffer_base = 0x80000000,
		.framebuffer_size = 4096,
		.horizontal_resolution = 32,
		.vertical_resolution = 32,
		.pixel_format = 1,
		.pixels_per_scanline = 32,
	};
	hob_size = sizeof(graphics);
	reports = 0;
	memset(&reported, 0, sizeof(reported));
	return 0;
}

static void test_exact_allocation(void **state)
{
	fsp_report_framebuffer_info(0x90000000, LB_FB_ORIENTATION_NORMAL);
	assert_int_equal(reports, 1);
	assert_int_equal(reported.physical_address, 0x90000000);
	assert_int_equal(reported.bytes_per_line, 128);
	assert_int_equal(reported.x_resolution, 32);
	assert_int_equal(reported.y_resolution, 32);
}

static void test_footprint_exceeds_allocation(void **state)
{
	graphics.framebuffer_size--;
	fsp_report_framebuffer_info(0x90000000, LB_FB_ORIENTATION_NORMAL);
	assert_int_equal(reports, 0);
}

static void test_stride_overflow(void **state)
{
	graphics.pixels_per_scanline = UINT32_MAX;
	graphics.vertical_resolution = UINT32_MAX;
	graphics.framebuffer_size = UINT32_MAX;
	fsp_report_framebuffer_info(0x90000000, LB_FB_ORIENTATION_NORMAL);
	assert_int_equal(reports, 0);
}

static void test_truncated_hob(void **state)
{
	hob_size = sizeof(graphics) - 1;
	fsp_report_framebuffer_info(0x90000000, LB_FB_ORIENTATION_NORMAL);
	assert_int_equal(reports, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_exact_allocation, setup),
		cmocka_unit_test_setup(test_footprint_exceeds_allocation, setup),
		cmocka_unit_test_setup(test_stride_overflow, setup),
		cmocka_unit_test_setup(test_truncated_hob, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
