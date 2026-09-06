/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <bootsplash.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static unsigned int allocations, frees, releases, loads;
static bool fail_allocation, allocation_live;
static uint8_t allocation[4096];
static uint8_t bitmap[4096];
static size_t bitmap_size;
static uint8_t framebuffer[128];
static struct lb_framebuffer fb;

static void *bmp_test_malloc(size_t size)
{
	allocations++;
	if (fail_allocation)
		return NULL;
	assert_false(allocation_live);
	assert_true(size <= sizeof(allocation));
	memset(allocation, 0xa5, sizeof(allocation));
	allocation_live = true;
	return allocation;
}

static void bmp_test_free(void *pointer)
{
	if (!pointer)
		return;
	assert_ptr_equal(pointer, allocation);
	assert_true(allocation_live);
	allocation_live = false;
	frees++;
}

#define malloc bmp_test_malloc
#define free bmp_test_free
#define main coreboot_main
#include "../../src/drivers/intel/fsp2_0/fsp_gop_blt.c"
#include "../../src/lib/render_bmp.c"
#undef main
#undef malloc
#undef free

void *bmp_load_logo_by_type(enum bootsplash_type type, size_t *size)
{
	loads++;
	*size = bitmap_size;
	return bitmap_size ? bitmap : NULL;
}

void *bmp_load_logo(size_t *size)
{
	return bmp_load_logo_by_type(BOOTSPLASH_CENTER, size);
}

void bmp_release_logo(void)
{
	releases++;
}

const struct lb_framebuffer *get_lb_framebuffer(void)
{
	return &fb;
}

static struct bmp_image_header *make_bitmap(unsigned int bpp, unsigned int width,
					  unsigned int height)
{
	struct bmp_image_header *header = (void *)bitmap;
	size_t palette_size = bpp <= 8 ? 2 : 0;
	size_t row_size = ((width * bpp + 31) / 32) * 4;

	memset(bitmap, 0, sizeof(bitmap));
	bitmap_size = sizeof(*header) + palette_size * sizeof(struct bmp_color_map) +
		row_size * height;
	assert_true(bitmap_size <= sizeof(bitmap));
	*header = (struct bmp_image_header) {
		.CharB = 'B', .CharM = 'M', .Size = bitmap_size,
		.HeaderSize = 40, .Planes = 1, .PixelWidth = width, .PixelHeight = height,
		.BitPerPixel = bpp, .ImageOffset = sizeof(*header) + palette_size * 4,
		.NumberOfColors = palette_size,
	};
	if (palette_size) {
		struct bmp_color_map *palette = (void *)(bitmap + sizeof(*header));

		palette[1] = (struct bmp_color_map) { .Blue = 1, .Green = 2, .Red = 3 };
	} else {
		for (size_t y = 0; y < height; y++)
			for (size_t x = 0; x < width; x++)
				bitmap[header->ImageOffset + y * row_size + x * (bpp / 8)] =
					y * width + x + 1;
	}
	return header;
}

static int setup(void **state)
{
	allocations = frees = releases = loads = 0;
	fail_allocation = allocation_live = false;
	boot_splash_handoff_valid = false;
	memset(framebuffer, 0xcc, sizeof(framebuffer));
	fb = (struct lb_framebuffer) {
		.physical_address = (uintptr_t)(framebuffer + 16),
		.x_resolution = 4, .y_resolution = 4, .bytes_per_line = 20,
		.bits_per_pixel = 32, .red_mask_pos = 16, .red_mask_size = 8,
		.green_mask_pos = 8, .green_mask_size = 8, .blue_mask_size = 8,
		.reserved_mask_pos = 24, .reserved_mask_size = 8,
	};
	make_bitmap(24, 3, 2);
	return 0;
}

static void expect_bad_bitmap(uintptr_t address, size_t size)
{
	uintptr_t blt = 1;
	size_t blt_size = 1;
	uint32_t width = 1, height = 1;

	assert_false(convert_bmp_to_blt(address, size, &blt, &blt_size, &height, &width,
				      LB_FB_ORIENTATION_NORMAL));
	assert_int_equal(blt, 0);
	assert_int_equal(blt_size, 0);
	assert_int_equal(width, 0);
	assert_int_equal(height, 0);
	assert_false(allocation_live);
}

static void test_malformed(void **state)
{
	struct bmp_image_header original = *(struct bmp_image_header *)bitmap;
	struct bmp_image_header *header = (void *)bitmap;

	for (size_t size = 0; size < original.Size; size++) {
		*header = original;
		header->Size = size;
		expect_bad_bitmap((uintptr_t)bitmap, size);
	}
#define BAD_FIELD(field, value) do { \
	*header = original; \
	header->field = value; \
	expect_bad_bitmap((uintptr_t)bitmap, bitmap_size); \
} while (0)
	BAD_FIELD(CharB, 'X');
	BAD_FIELD(Size, bitmap_size + 1);
	BAD_FIELD(HeaderSize, 12);
	BAD_FIELD(Planes, 2);
	BAD_FIELD(CompressionType, 1);
	BAD_FIELD(ImageOffset, sizeof(*header) - 1);
	BAD_FIELD(ImageOffset, bitmap_size + 1);
	BAD_FIELD(ImageSize, 1);
	BAD_FIELD(ImageSize, UINT32_MAX);
	BAD_FIELD(PixelWidth, 0);
	BAD_FIELD(PixelHeight, 0);
	BAD_FIELD(PixelHeight, UINT32_MAX);
	BAD_FIELD(PixelWidth, 0x40000000);
	BAD_FIELD(BitPerPixel, 16);
#undef BAD_FIELD
	expect_bad_bitmap(0, bitmap_size);
	expect_bad_bitmap(UINTPTR_MAX - 1, bitmap_size);
	assert_int_equal(allocations, 0);
}

static void test_conversion(void **state)
{
	static const unsigned int order[][6] = {
		{ 4, 5, 6, 1, 2, 3 }, { 3, 2, 1, 6, 5, 4 },
		{ 6, 3, 5, 2, 4, 1 }, { 1, 4, 2, 5, 3, 6 },
	};
	uintptr_t blt;
	size_t size;
	uint32_t width, height;

	for (unsigned int bpp = 24; bpp <= 32; bpp += 8) {
		make_bitmap(bpp, 3, 2);
		for (unsigned int orientation = 0; orientation < 4; orientation++) {
			assert_true(convert_bmp_to_blt((uintptr_t)bitmap, bitmap_size, &blt,
						     &size, &height, &width, orientation));
			assert_int_equal(size, 24);
			assert_int_equal(width, orientation < 2 ? 3 : 2);
			assert_int_equal(height, orientation < 2 ? 2 : 3);
			for (size_t i = 0; i < 6; i++) {
				struct blt_pixel *pixels = (void *)blt;

				assert_int_equal(pixels[i].Blue, order[orientation][i]);
				assert_int_equal(pixels[i].Reserved, 0);
			}
			bmp_test_free((void *)blt);
		}
	}
	assert_int_equal(allocations, frees);
	assert_false(convert_bmp_to_blt((uintptr_t)bitmap, bitmap_size,
				      &blt, &size, &height, &width, 4));
	assert_int_equal(blt | size | height | width, 0);
	fail_allocation = true;
	expect_bad_bitmap((uintptr_t)bitmap, bitmap_size);
	blt = size = width = height = 1;
	assert_false(convert_bmp_to_blt((uintptr_t)bitmap, bitmap_size,
				      &blt, &size, &height, NULL, 0));
	assert_int_equal(blt | size | height, 0);
}

static void test_palette(void **state)
{
	static const unsigned int bits[] = { 1, 4, 8 };
	uintptr_t blt;
	size_t size;
	uint32_t width, height;

	for (size_t i = 0; i < ARRAY_SIZE(bits); i++) {
		struct bmp_image_header *header = make_bitmap(bits[i], 9, 2);

		bitmap[header->ImageOffset] = bits[i] == 1 ? 0x80 : bits[i] == 4 ? 0x10 : 1;
		assert_true(convert_bmp_to_blt((uintptr_t)bitmap, bitmap_size,
					     &blt, &size, &height, &width, 0));
		assert_int_equal(((struct blt_pixel *)blt)[9].Blue, 1);
		for (size_t j = 0; j < width * height; j++)
			assert_int_equal(((struct blt_pixel *)blt)[j].Reserved, 0);
		bmp_test_free((void *)blt);
		header->NumberOfColors = 1;
		expect_bad_bitmap((uintptr_t)bitmap, bitmap_size);
		header->NumberOfColors = 257;
		expect_bad_bitmap((uintptr_t)bitmap, bitmap_size);
	}
	assert_int_equal(allocations, frees);
}

static struct logo_config logo_config(void)
{
	return (struct logo_config) {
		.framebuffer_base = fb.physical_address,
		.horizontal_resolution = fb.x_resolution,
		.vertical_resolution = fb.y_resolution,
		.bytes_per_scanline = fb.bytes_per_line,
	};
}

static void test_render(void **state)
{
	struct logo_config config = logo_config();
	struct lb_boot_splash handoff;

	assert_int_equal(load_and_render_logo_to_framebuffer(BOOTSPLASH_CENTER, &config), 0);
	assert_int_equal(allocations, 1);
	assert_int_equal(frees, 1);
	assert_int_equal(releases, 1);
	assert_true(bootsplash_get_handoff(&handoff));
	assert_int_equal(handoff.image_width, 3);
	assert_int_equal(handoff.image_height, 2);
	for (size_t i = 0; i < sizeof(framebuffer); i++) {
		bool in_logo = (i >= 36 && i < 48) || (i >= 56 && i < 68);

		if (!in_logo)
			assert_int_equal(framebuffer[i], 0xcc);
	}
}

static void test_render_failure(void **state)
{
	struct logo_config config = logo_config();
	struct logo_config original = config;

	config.horizontal_resolution = 2;
	assert_int_equal(load_and_render_logo_to_framebuffer(BOOTSPLASH_CENTER, &config), -1);
	config = original;
	config.logo_bottom_margin = 255;
	assert_int_equal(load_and_render_logo_to_framebuffer(BOOTSPLASH_FOOTER, &config), -1);
	assert_int_equal(allocations, 2);
	assert_int_equal(frees, 2);
	assert_int_equal(releases, 2);
	config = original;
	config.bytes_per_scanline = 15;
	assert_int_equal(load_and_render_logo_to_framebuffer(BOOTSPLASH_CENTER, &config), -1);
	config = original;
	config.framebuffer_base = UINTPTR_MAX - 20;
	assert_int_equal(load_and_render_logo_to_framebuffer(BOOTSPLASH_CENTER, &config), -1);
	config = original;
	bitmap_size = 0;
	assert_int_equal(load_and_render_logo_to_framebuffer(BOOTSPLASH_CENTER, &config), -1);
	assert_false(boot_splash_handoff_valid);
	assert_int_equal(allocations, frees);
	for (size_t i = 0; i < sizeof(framebuffer); i++)
		assert_int_equal(framebuffer[i], 0xcc);
}

static void test_framebuffer_format(void **state)
{
	struct logo_config config = { 0 };

	fb.bits_per_pixel = 24;
	render_logo_to_framebuffer(&config);
	assert_int_equal(loads, 0);
	fb.bits_per_pixel = 32;
	fb.red_mask_pos = 0;
	fb.blue_mask_pos = 16;
	render_logo_to_framebuffer(&config);
	assert_int_equal(loads, 0);
	fb.red_mask_pos = 16;
	fb.blue_mask_pos = 0;
	fb.orientation = LB_FB_ORIENTATION_LEFT_UP;
	render_logo_to_framebuffer(&config);
	assert_int_equal(config.panel_orientation, LB_FB_ORIENTATION_LEFT_UP);
	assert_int_equal(loads, 1);
	assert_int_equal(allocations, frees);
	assert_true(boot_splash_handoff_valid);
}

static void test_fsp_outputs(void **state)
{
	efi_uintn_t logo = 0, blt = 0, size = 0;
	struct { uint32_t size; uint32_t canary; } output = { .canary = 0x12345678 };
	uint32_t height, width;

	fsp_load_and_convert_bmp_to_gop_blt(&logo, &output.size, &blt, &size,
					 &height, &width, 0);
	assert_int_equal(output.size, bitmap_size);
	assert_int_equal(output.canary, 0x12345678);
	assert_int_equal(blt, (uintptr_t)allocation);
	bmp_test_free((void *)(uintptr_t)blt);
	bmp_release_logo();
	bitmap_size = 0;
	fsp_load_and_convert_bmp_to_gop_blt(&logo, &output.size, &blt, &size,
					 &height, &width, 0);
	assert_int_equal(logo | output.size | blt | size | height | width, 0);
	assert_int_equal(output.canary, 0x12345678);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_malformed, setup),
		cmocka_unit_test_setup(test_conversion, setup),
		cmocka_unit_test_setup(test_palette, setup),
		cmocka_unit_test_setup(test_render, setup),
		cmocka_unit_test_setup(test_render_failure, setup),
		cmocka_unit_test_setup(test_framebuffer_format, setup),
		cmocka_unit_test_setup(test_fsp_outputs, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
