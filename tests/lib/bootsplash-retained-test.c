/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <bootsplash.h>
#include <cbfs.h>
#include <imd.h>
#include <stdlib.h>
#include <string.h>
#include <symbols.h>

static bool fail_blt_allocation;
static size_t last_blt_allocation;
static unsigned int blt_allocations;

static void *test_blt_calloc(size_t count, size_t size)
{
	last_blt_allocation = count * size;
	blt_allocations++;
	if (fail_blt_allocation)
		return NULL;
	return calloc(count, size);
}

/* Real owners are included so each test can reset their private boot state. */
#include "../../src/lib/imd_cbmem.c"
#include "../../src/lib/bmp_logo.c"
#define main coreboot_main
#define calloc test_blt_calloc
#include "../../src/lib/render_bmp.c"
#undef calloc
#undef main

TEST_REGION_UNALLOCATED(program, 0x100000, 0x10000);
TEST_REGION_UNALLOCATED(stack, 0x110000, 0x10000);

static uint8_t cbmem_storage[4 * MiB] __aligned(4096);
static uint8_t source_bitmap[1 * MiB];
static uint32_t pixels[3840 * 2160];
static uint8_t footer_bitmap[1 * MiB];
static size_t footer_size;
static size_t source_size;
static unsigned int media_loads;
static bool inject_remove_failure, inject_allocation_failure, wakeup_s3;
static struct lb_framebuffer framebuffer;
uintptr_t cbmem_top_chipset(void)
{
	return _cbmem_top_ptr;
}

void cbmem_run_init_hooks(int recovery)
{
}

bool romstage_handoff_is_resume(void)
{
	return wakeup_s3;
}

const struct lb_framebuffer *get_lb_framebuffer(void)
{
	return &framebuffer;
}

/* Only the CBFS media edge is modeled; its allocator and both owners are real. */
void *_cbfs_alloc(const char *name, cbfs_allocator_t allocator, void *argument,
	size_t *size, bool force_ro, enum cbfs_type *type)
{
	void *buffer;

	const void *data = source_bitmap;
	size_t data_size = source_size;

	if (!strcmp(name, "footer_logo.bmp")) {
		if (!footer_size)
			return NULL;
		data = footer_bitmap;
		data_size = footer_size;
	} else {
		assert_string_equal(name, "logo.bmp");
	}
	media_loads++;
	buffer = allocator(argument, data_size, NULL);
	if (!buffer)
		return NULL;
	memcpy(buffer, data, data_size);
	*size = data_size;
	if (inject_remove_failure)
		assert_non_null(cbmem_add(0xdeadbeef, 16384));
	if (inject_allocation_failure) {
		void *base;
		size_t used;

		assert_int_equal(cbmem_get_region(&base, &used), 0);
		assert_int_equal(imd_limit_size(&imd, used), 0);
	}
	return buffer;
}

static void make_source(unsigned int width, unsigned int height)
{
	struct bmp_image_header *header = (void *)source_bitmap;
	size_t row_size = ALIGN_UP(width * 3U, 4);

	source_size = sizeof(*header) + row_size * height;
	assert_true(source_size <= sizeof(source_bitmap));
	memset(source_bitmap, 0, source_size);
	*header = (struct bmp_image_header) {
		.CharB = 'B', .CharM = 'M', .Size = source_size,
		.ImageOffset = sizeof(*header), .HeaderSize = 40,
		.PixelWidth = width, .PixelHeight = height, .Planes = 1, .BitPerPixel = 24,
		.ImageSize = row_size * height,
	};
	for (size_t y = 0; y < height; y++)
		for (size_t x = 0; x < width; x++)
			source_bitmap[sizeof(*header) + y * row_size + x * 3] =
				y * width + x + 1;
}

static int setup(void **state)
{
	memset(cbmem_storage, 0, sizeof(cbmem_storage));
	_cbmem_top_ptr = (uintptr_t)(cbmem_storage + sizeof(cbmem_storage));
	imd_handle_init(&imd, (void *)_cbmem_top_ptr);
	assert_int_equal(imd_create_tiered_empty(&imd, 4096, 4096, 1024, 32), 0);
	cbmem_initialized = 1;
	logo_entry = NULL;
	boot_splash_handoff_valid = false;
	memset(&boot_splash_handoff, 0, sizeof(boot_splash_handoff));
	memset(pixels, 0xa5, sizeof(pixels));
	media_loads = 0;
	fail_blt_allocation = false;
	last_blt_allocation = 0;
	blt_allocations = 0;
	footer_size = 0;
	inject_remove_failure = false;
	inject_allocation_failure = false;
	wakeup_s3 = false;
	framebuffer = (struct lb_framebuffer) {
		.physical_address = (uintptr_t)pixels, .x_resolution = 512,
		.y_resolution = 512, .bytes_per_line = 512 * 4, .bits_per_pixel = 32,
		.red_mask_pos = 16, .red_mask_size = 8, .green_mask_pos = 8,
		.green_mask_size = 8, .blue_mask_pos = 0, .blue_mask_size = 8,
		.reserved_mask_pos = 24, .reserved_mask_size = 8,
	};
	make_source(3, 2);
	return 0;
}

void timestamp_add_now(enum timestamp_id id)
{
	assert_int_equal(id, TS_FIRMWARE_SPLASH_RENDERED);
}

static void check_retained_owner(const struct lb_boot_splash *splash)
{
	const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_BOOT_SPLASH);
	uintptr_t bitmap = splash->bmp_address;
	size_t span = ALIGN_UP(splash->bmp_size, 4096);

	assert_non_null(entry);
	assert_int_equal(bitmap % 4096, 0);
	assert_true(bitmap >= (uintptr_t)cbmem_entry_start(entry));
	assert_true(bitmap - (uintptr_t)cbmem_entry_start(entry) + span <=
		cbmem_entry_size(entry));
	release_logo(NULL);
	assert_ptr_equal(cbmem_entry_find(CBMEM_ID_BOOT_SPLASH), entry);
}

static void test_rotated_retention(void **state)
{
	static const uint8_t expected[4][6] = {
		{ 4, 5, 6, 1, 2, 3 }, { 3, 2, 1, 6, 5, 4 },
		{ 6, 3, 5, 2, 4, 1 }, { 1, 4, 2, 5, 3, 6 },
	};

	for (unsigned int orientation = 0; orientation < ARRAY_SIZE(expected); orientation++) {
		struct lb_boot_splash splash;
		const struct bmp_image_header *retained;
		size_t row_size;

		setup(state);
		framebuffer.orientation = orientation;
		/* Exercise the production renderer and its actual CBMEM allocation. */
		bootsplash_render_primary();
		assert_true(bootsplash_get_handoff(&splash));
		assert_int_equal(splash.tag, 0x51);
		assert_int_equal(splash.size, 48);
		assert_int_equal(splash.revision, 2);
		assert_int_equal(splash.flags, 3);
		retained = (const void *)(uintptr_t)splash.bmp_address;
		assert_int_equal(retained->PixelWidth, splash.image_width);
		assert_int_equal(retained->PixelHeight, splash.image_height);
		if (!orientation)
			assert_memory_equal(retained, source_bitmap, source_size);
		else
			assert_int_equal(retained->BitPerPixel, 24);
		row_size = ALIGN_UP(retained->PixelWidth * (retained->BitPerPixel / 8), 4);
		for (size_t index = 0; index < 6; index++) {
			size_t x = index % splash.image_width;
			size_t y = index / splash.image_width;
			const uint8_t *bitmap = (const uint8_t *)retained + retained->ImageOffset;

			assert_int_equal(pixels[(splash.image_offset_y + y) * 512 +
				splash.image_offset_x + x], expected[orientation][index]);
			assert_int_equal(bitmap[(splash.image_height - y - 1) * row_size +
				x * (retained->BitPerPixel / 8)], expected[orientation][index]);
		}
		for (size_t y = 0; y < 512; y++)
			for (size_t x = 0; x < 512; x++)
				if (x < splash.image_offset_x || y < splash.image_offset_y ||
				    x >= splash.image_offset_x + splash.image_width ||
				    y >= splash.image_offset_y + splash.image_height)
					assert_int_equal(pixels[y * 512 + x], 0);
		bootsplash_render_primary();
		assert_int_equal(media_loads, 1);
		assert_null(logo_entry);
		check_retained_owner(&splash);
	}
}

static void test_offset_small_root(void **state)
{
	struct lb_boot_splash splash;
	const struct cbmem_entry *entry;

	/* Alternate supported IMD geometry, not the default 1024/32 small tier. */
	imd_handle_init(&imd, (void *)_cbmem_top_ptr);
	assert_int_equal(imd_create_tiered_empty(&imd, 4096, 4096, 4096, 256), 0);
	assert_non_null(cbmem_add(0xfeed0001, 256));
	bootsplash_render_primary();
	assert_true(bootsplash_get_handoff(&splash));
	entry = cbmem_entry_find(CBMEM_ID_BOOT_SPLASH);
	assert_int_not_equal((uintptr_t)cbmem_entry_start(entry) % 4096, 0);
	check_retained_owner(&splash);
}

static void test_retention_failures(void **state)
{
	struct lb_boot_splash splash;

	for (unsigned int mode = 0; mode < 2; mode++) {
		setup(state);
		framebuffer.orientation = LB_FB_ORIENTATION_BOTTOM_UP;
		inject_remove_failure = mode == 0;
		inject_allocation_failure = mode == 1;
		if (mode == 1) {
			/* Rotation needs another CBMEM page for its padded rows. */
			framebuffer.orientation = LB_FB_ORIENTATION_RIGHT_UP;
			make_source(400, 3);
		}
		bootsplash_render_primary();
		assert_false(bootsplash_get_handoff(&splash));
		for (size_t index = 0; index < ARRAY_SIZE(pixels); index++)
			assert_int_equal(pixels[index], 0xa5a5a5a5);
		if (mode != 0)
			assert_null(cbmem_entry_find(CBMEM_ID_BOOT_SPLASH));
	}
}

static void test_s3_does_not_allocate(void **state)
{
	struct lb_boot_splash splash;

	wakeup_s3 = true;
	bootsplash_render_primary();
	assert_false(bootsplash_get_handoff(&splash));
	assert_int_equal(media_loads, 0);
	wakeup_s3 = false;
	bootsplash_render_primary();
	assert_true(bootsplash_get_handoff(&splash));
	assert_int_equal(media_loads, 1);
}

static void test_rejected_conversion_has_defined_outputs(void **state)
{
	for (unsigned int mode = 0; mode < 3; mode++) {
		uintptr_t blt = UINTPTR_MAX;
		size_t size = SIZE_MAX;
		uint32_t width = UINT32_MAX, height = UINT32_MAX;
		struct bmp_image_header *header = (void *)source_bitmap;
		struct lb_boot_splash splash;

		setup(state);
		if (mode == 0)
			header->CharB = 0;
		else if (mode == 1)
			header->ImageOffset = source_size;
		else
			source_size = sizeof(*header) - 1;
		convert_bmp_to_blt((uintptr_t)source_bitmap, source_size,
			&blt, &size, &height, &width, LB_FB_ORIENTATION_NORMAL);
		assert_int_equal(blt, 0);
		assert_int_equal(size, 0);
		assert_int_equal(width, 0);
		assert_int_equal(height, 0);
		bootsplash_render_primary();
		assert_false(bootsplash_get_handoff(&splash));
		for (size_t index = 0; index < ARRAY_SIZE(pixels); index++)
			assert_int_equal(pixels[index], 0xa5a5a5a5);
	}
}

static void test_indexed_image_retains_bgrt_format(void **state)
{
	struct bmp_image_header *header = (void *)source_bitmap;
	struct bmp_color_map *colors = (void *)(header + 1);
	struct lb_boot_splash splash;
	const struct bmp_image_header *retained;
	uint8_t *data;

	memset(source_bitmap, 0, sizeof(source_bitmap));
	source_size = sizeof(*header) + 256 * sizeof(*colors) + 8;
	*header = (struct bmp_image_header) {
		.CharB = 'B', .CharM = 'M', .Size = source_size,
		.ImageOffset = sizeof(*header) + 256 * sizeof(*colors), .HeaderSize = 40,
		.PixelWidth = 3, .PixelHeight = 2, .Planes = 1, .BitPerPixel = 8,
		.ImageSize = 8,
	};
	for (size_t index = 0; index < 256; index++)
		colors[index].Blue = index;
	data = source_bitmap + header->ImageOffset;
	data[0] = 1; data[1] = 2; data[2] = 3;
	data[4] = 4; data[5] = 5; data[6] = 6;
	bootsplash_render_primary();
	assert_true(bootsplash_get_handoff(&splash));
	retained = (const void *)(uintptr_t)splash.bmp_address;
	assert_int_equal(retained->BitPerPixel, 24);
	assert_int_equal(retained->PixelWidth, 3);
	assert_int_equal(retained->PixelHeight, 2);
	assert_int_equal(pixels[splash.image_offset_y * 512 + splash.image_offset_x], 4);
	check_retained_owner(&splash);
}

static void check_footer_composition(bool overlaps)
{
	struct lb_boot_splash splash;
	struct logo_config config = {
		.framebuffer_base = (uintptr_t)pixels,
		.horizontal_resolution = 640, .vertical_resolution = 480,
		.bytes_per_scanline = 640 * sizeof(pixels[0]),
	};

	make_source(200, 100);
	footer_size = source_size;
	memcpy(footer_bitmap, source_bitmap, footer_size);
	make_source(overlaps ? 400 : 200, overlaps ? 400 : 200);
	render_logo_to_framebuffer(&config);
	assert_int_equal(bootsplash_get_handoff(&splash), !overlaps);
	/* The footer still renders, including when it replaces primary pixels. */
	assert_int_equal(pixels[380 * 640 + 220], (uint8_t)(99 * 200 + 1));
	assert_null(cbmem_entry_find(CBMEM_ID_BMP_LOGO));
	if (!overlaps) {
		assert_int_equal(splash.image_width, 200);
		assert_memory_equal((const void *)(uintptr_t)splash.bmp_address,
			source_bitmap, source_size);
	}
}

static void test_overlapping_footer_suppresses_handoff(void **state)
{
	check_footer_composition(true);
}

static void test_separate_footer_preserves_handoff(void **state)
{
	check_footer_composition(false);
}

static void test_scratch_allocation_failure_is_nonfatal(void **state)
{
	struct lb_boot_splash splash;

	/* A valid 24-bit source needs exactly 1 MiB of decoded scratch space. */
	make_source(512, 512);
	fail_blt_allocation = true;
	bootsplash_render_primary();
	assert_int_equal(last_blt_allocation, MiB);
	assert_false(bootsplash_get_handoff(&splash));
	assert_null(cbmem_entry_find(CBMEM_ID_BOOT_SPLASH));
	for (size_t index = 0; index < ARRAY_SIZE(pixels); index++)
		assert_int_equal(pixels[index], 0xa5a5a5a5);
	fail_blt_allocation = false;
	bootsplash_render_primary();
	assert_true(bootsplash_get_handoff(&splash));
}

static void test_hidpi_primary_matches_retained_bitmap(void **state)
{
	static const struct {
		uint32_t width, height, logo_width, logo_height, scale;
	} cases[] = {
		{ 2160, 1440, 234, 234, 2 },
		{ 1920, 1080, 3, 2, 1 },
		{ 1922, 1082, 3, 2, 2 },
		{ 2161, 1440, 3, 2, 1 },
		{ 1920, 1440, 3, 2, 1 },
		{ 2160, 1080, 3, 2, 1 },
		{ 2160, 1440, 300, 300, 1 }, /* Scaled BMP would exceed retention. */
		{ 2160, 1440, 1100, 1, 1 }, /* Scaled image would not fit. */
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct lb_boot_splash splash;
		const struct bmp_image_header *bitmap;

		setup(state);
		framebuffer.x_resolution = cases[i].width;
		framebuffer.y_resolution = cases[i].height;
		framebuffer.bytes_per_line = cases[i].width * sizeof(pixels[0]);
		make_source(cases[i].logo_width, cases[i].logo_height);
		bootsplash_render_primary();
		assert_true(bootsplash_get_handoff(&splash));
		assert_int_equal(blt_allocations, 1);
		assert_int_equal(last_blt_allocation,
			cases[i].logo_width * cases[i].logo_height * sizeof(pixels[0]));
		assert_int_equal(splash.image_width, cases[i].logo_width * cases[i].scale);
		assert_int_equal(splash.image_height, cases[i].logo_height * cases[i].scale);
		assert_int_equal(splash.image_offset_x,
			(cases[i].width - splash.image_width) / 2);
		assert_int_equal(splash.image_offset_y,
			(cases[i].height - splash.image_height) / 2);
		bitmap = (const void *)(uintptr_t)splash.bmp_address;
		assert_int_equal(bitmap->PixelWidth, splash.image_width);
		assert_int_equal(bitmap->PixelHeight, splash.image_height);
		assert_int_equal(bitmap->BitPerPixel, 24);
		size_t row_size = ALIGN_UP(bitmap->PixelWidth * (bitmap->BitPerPixel / 8), 4);
		for (size_t y = 0; y < splash.image_height; y++) {
			for (size_t x = 0; x < splash.image_width; x++) {
				uint8_t expected = (cases[i].logo_height - y / cases[i].scale - 1) *
					cases[i].logo_width + x / cases[i].scale + 1;
				const uint8_t *data = (const uint8_t *)bitmap + bitmap->ImageOffset;

				assert_int_equal(pixels[(splash.image_offset_y + y) *
					cases[i].width + splash.image_offset_x + x], expected);
				assert_int_equal(data[(splash.image_height - y - 1) * row_size +
					x * (bitmap->BitPerPixel / 8)], expected);
			}
		}
		check_retained_owner(&splash);
	}
}

static void test_hidpi_retention_failure_leaves_framebuffer_untouched(void **state)
{
	struct lb_boot_splash splash;

	framebuffer.x_resolution = 2160;
	framebuffer.y_resolution = 1440;
	framebuffer.bytes_per_line = 2160 * sizeof(pixels[0]);
	make_source(234, 234);
	inject_allocation_failure = true;
	bootsplash_render_primary();
	assert_false(bootsplash_get_handoff(&splash));
	assert_int_equal(blt_allocations, 1);
	assert_int_equal(last_blt_allocation, 234 * 234 * sizeof(pixels[0]));
	for (size_t i = 0; i < ARRAY_SIZE(pixels); i++)
		assert_int_equal(pixels[i], 0xa5a5a5a5);
}

static void test_hidpi_placement_falls_back_to_original(void **state)
{
	struct lb_boot_splash splash;
	struct logo_config config = {
		.framebuffer_base = (uintptr_t)pixels,
		.horizontal_resolution = 2160, .vertical_resolution = 1440,
		.bytes_per_scanline = 2160 * sizeof(pixels[0]),
		.halignment = FW_SPLASH_HALIGNMENT_CENTER,
		.valignment = FW_SPLASH_VALIGNMENT_MIDDLE,
	};

	make_source(100, 500);
	render_logo_to_framebuffer(&config);
	assert_true(bootsplash_get_handoff(&splash));
	assert_int_equal(splash.image_width, 100);
	assert_int_equal(splash.image_height, 500);
	assert_int_equal(splash.image_offset_x, 1030);
	assert_int_equal(splash.image_offset_y, 720);
	assert_memory_equal((const void *)(uintptr_t)splash.bmp_address,
		source_bitmap, source_size);
}

static void test_retained_size_rejection_preserves_previous_image(void **state)
{
	size_t size;
	const struct cbmem_entry *entry;

	assert_non_null(bmp_retain_logo_from_blt(source_bitmap, 3, 2, 1, &size));
	entry = cbmem_entry_find(CBMEM_ID_BOOT_SPLASH);
	/* One-pixel rows need padding; source BLT still fits its backing array. */
	assert_null(bmp_retain_logo_from_blt(source_bitmap, 1,
		(BMP_LOGO_MAX_SIZE - sizeof(struct bmp_image_header)) / 4 + 1, 1, &size));
	assert_int_equal(size, 0);
	assert_ptr_equal(cbmem_entry_find(CBMEM_ID_BOOT_SPLASH), entry);
}

static void test_32_bit_source_is_exported_as_24_bit(void **state)
{
	struct bmp_image_header *header = (void *)source_bitmap;
	struct lb_boot_splash splash;

	header->BitPerPixel = 32;
	for (size_t i = 0; i < 6; i++) {
		uint8_t *pixel = source_bitmap + header->ImageOffset + i * 4;

		pixel[0] = i + 1;
		pixel[1] = i + 2;
		pixel[2] = i + 3;
		pixel[3] = 0xff;
	}
	bootsplash_render_primary();
	assert_true(bootsplash_get_handoff(&splash));
	const struct bmp_image_header *retained = (const void *)(uintptr_t)splash.bmp_address;
	const uint8_t *data = (const uint8_t *)retained + retained->ImageOffset;

	assert_int_equal(retained->BitPerPixel, 24);
	for (size_t y = 0; y < 2; y++) {
		for (size_t x = 0; x < 3; x++) {
			size_t i = y * 3 + x;

			for (size_t channel = 0; channel < 3; channel++)
				assert_int_equal(data[y * 12 + x * 3 + channel], i + channel + 1);
			assert_int_equal(pixels[(splash.image_offset_y + 1 - y) * 512 +
				splash.image_offset_x + x], (i + 3) << 16 | (i + 2) << 8 | (i + 1));
		}
	}
}

static void test_retained_bitmap_uses_plymouth_format(void **state)
{
	const struct blt_pixel source[] = {
		{ .Blue = 1, .Green = 2, .Red = 3, .Reserved = 0xff },
		{ .Blue = 4, .Green = 5, .Red = 6, .Reserved = 0xff },
		{ .Blue = 7, .Green = 8, .Red = 9, .Reserved = 0xff },
		{ .Blue = 10, .Green = 11, .Red = 12, .Reserved = 0xff },
		{ .Blue = 13, .Green = 14, .Red = 15, .Reserved = 0xff },
		{ .Blue = 16, .Green = 17, .Red = 18, .Reserved = 0xff },
	};

	for (unsigned int scale = 1; scale <= 2; scale++) {
		size_t size, pitch = ALIGN_UP(3 * scale * 3, 4);
		const struct bmp_image_header *bmp = bmp_retain_logo_from_blt(source,
			3, 2, scale, &size);

		assert_non_null(bmp);
		/* Plymouth's BMP reader accepts only uncompressed 24-bit pixels. */
		assert_int_equal(bmp->BitPerPixel, 24);
		assert_int_equal(bmp->CompressionType, 0);
		assert_int_equal(bmp->ImageSize, pitch * 2 * scale);
		assert_int_equal(size, sizeof(*bmp) + bmp->ImageSize);
		const uint8_t *data = (const uint8_t *)(bmp + 1);

		for (size_t y = 0; y < 2 * scale; y++) {
			for (size_t x = 0; x < 3 * scale; x++) {
				const struct blt_pixel *pixel = &source[(1 - y / scale) * 3 +
					x / scale];

				assert_int_equal(data[y * pitch + x * 3], pixel->Blue);
				assert_int_equal(data[y * pitch + x * 3 + 1], pixel->Green);
				assert_int_equal(data[y * pitch + x * 3 + 2], pixel->Red);
			}
			for (size_t x = 3 * scale * 3; x < pitch; x++)
				assert_int_equal(data[y * pitch + x], 0);
		}
	}
}

static void test_double_density_display_sizes(void **state)
{
	static const struct {
		uint32_t width, height, logo_size;
	} cases[] = {
		{ 3840, 2160, 468 }, { 2160, 1440, 468 }, { 1922, 1082, 468 },
		{ 1920, 1080, 234 }, { 1280, 720, 234 }, { 320, 240, 234 },
		{ 2161, 1440, 234 }, { 1920, 1440, 234 }, { 2160, 1080, 234 },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct lb_boot_splash splash;
		const struct bmp_image_header *bmp;

		setup(state);
		framebuffer.x_resolution = cases[i].width;
		framebuffer.y_resolution = cases[i].height;
		framebuffer.bytes_per_line = cases[i].width * sizeof(pixels[0]);
		make_source(468, 468);
		bootsplash_render_primary();
		assert_true(bootsplash_get_handoff(&splash));
		assert_int_equal(splash.image_width, cases[i].logo_size);
		assert_int_equal(splash.image_height, cases[i].logo_size);
		assert_int_equal(splash.image_offset_x, (cases[i].width - cases[i].logo_size) / 2);
		assert_int_equal(splash.image_offset_y, (cases[i].height - cases[i].logo_size) / 2);
		assert_int_equal(blt_allocations, 1);
		assert_int_equal(last_blt_allocation, 468 * 468 * sizeof(pixels[0]));
		bmp = (const void *)(uintptr_t)splash.bmp_address;
		assert_int_equal(bmp->BitPerPixel, 24);
		assert_int_equal(bmp->PixelWidth, splash.image_width);
		assert_int_equal(bmp->PixelHeight, splash.image_height);
		size_t pitch = ALIGN_UP(bmp->PixelWidth * 3, 4);
		const uint8_t *data = (const uint8_t *)bmp + bmp->ImageOffset;

		for (size_t y = 0; y < splash.image_height; y++)
			for (size_t x = 0; x < splash.image_width; x++)
				assert_int_equal(pixels[(splash.image_offset_y + y) * cases[i].width +
					splash.image_offset_x + x],
					data[(splash.image_height - y - 1) * pitch + x * 3]);
		check_retained_owner(&splash);
	}
}

static void test_double_density_rotation_and_averaging(void **state)
{
	static const uint8_t expected[4][6] = {
		{ 16, 18, 20, 4, 6, 8 }, { 8, 6, 4, 20, 18, 16 },
		{ 20, 8, 18, 6, 16, 4 }, { 4, 16, 6, 18, 8, 20 },
	};

	for (unsigned int orientation = 0; orientation < ARRAY_SIZE(expected); orientation++) {
		struct lb_boot_splash splash;

		setup(state);
		framebuffer.orientation = orientation;
		make_source(6, 4);
		bootsplash_render_primary();
		assert_true(bootsplash_get_handoff(&splash));
		assert_int_equal(splash.image_width, orientation < 2 ? 3 : 2);
		assert_int_equal(splash.image_height, orientation < 2 ? 2 : 3);
		for (size_t i = 0; i < 6; i++)
			assert_int_equal(pixels[(splash.image_offset_y + i / splash.image_width) *
				512 + splash.image_offset_x + i % splash.image_width],
				expected[orientation][i]);
	}
}

static void test_double_density_averages_all_color_channels(void **state)
{
	static const uint32_t expected[] = { 0x23170b, 0x291b0d, 0x0b0703, 0x110b05 };
	struct lb_boot_splash splash;

	make_source(4, 4);
	for (size_t i = 0; i < 16; i++) {
		uint8_t *pixel = source_bitmap + sizeof(struct bmp_image_header) + i * 3;

		pixel[0] = i + 1;
		pixel[1] = (i + 1) * 2;
		pixel[2] = (i + 1) * 3 + 1;
	}
	bootsplash_render_primary();
	assert_true(bootsplash_get_handoff(&splash));
	assert_int_equal(splash.image_width, 2);
	assert_int_equal(splash.image_height, 2);
	for (size_t i = 0; i < ARRAY_SIZE(expected); i++)
		assert_int_equal(pixels[(splash.image_offset_y + i / 2) * 512 +
			splash.image_offset_x + i % 2], expected[i]);
}

static void test_double_density_requires_complete_pixel_blocks(void **state)
{
	struct lb_boot_splash splash;

	/* The default 3x2 fixture cannot describe complete 2x2 blocks. */
	bootsplash_render_primary();
	assert_false(bootsplash_get_handoff(&splash));
	assert_int_equal(pixels[0], 0xa5a5a5a5);
	assert_null(cbmem_entry_find(CBMEM_ID_BOOT_SPLASH));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_retained_size_rejection_preserves_previous_image, setup),
		cmocka_unit_test_setup(test_32_bit_source_is_exported_as_24_bit, setup),
		cmocka_unit_test_setup(test_retained_bitmap_uses_plymouth_format, setup),
		cmocka_unit_test_setup(test_hidpi_placement_falls_back_to_original, setup),
		cmocka_unit_test_setup(test_hidpi_retention_failure_leaves_framebuffer_untouched, setup),
		cmocka_unit_test_setup(test_hidpi_primary_matches_retained_bitmap, setup),
		cmocka_unit_test_setup(test_scratch_allocation_failure_is_nonfatal, setup),
		cmocka_unit_test_setup(test_overlapping_footer_suppresses_handoff, setup),
		cmocka_unit_test_setup(test_separate_footer_preserves_handoff, setup),
		cmocka_unit_test_setup(test_rotated_retention, setup),
		cmocka_unit_test_setup(test_offset_small_root, setup),
		cmocka_unit_test_setup(test_retention_failures, setup),
		cmocka_unit_test_setup(test_s3_does_not_allocate, setup),
		cmocka_unit_test_setup(test_rejected_conversion_has_defined_outputs, setup),
		cmocka_unit_test_setup(test_indexed_image_retains_bgrt_format, setup),
	};

	const struct CMUnitTest hidpi_tests[] = {
		cmocka_unit_test_setup(test_double_density_averages_all_color_channels, setup),
		cmocka_unit_test_setup(test_double_density_display_sizes, setup),
		cmocka_unit_test_setup(test_double_density_rotation_and_averaging, setup),
		cmocka_unit_test_setup(test_double_density_requires_complete_pixel_blocks, setup),
	};

	if (CONFIG_BMP_LOGO_SOURCE_SCALE == 2)
		return cmocka_run_group_tests(hidpi_tests, NULL, NULL);
	return cmocka_run_group_tests(tests, NULL, NULL);
}
