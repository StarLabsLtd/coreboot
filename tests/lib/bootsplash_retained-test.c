/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <bootsplash.h>
#include <bootmem.h>
#include <cbfs.h>
#include <device/device.h>
#include <imd.h>
#include <stdlib.h>
#include <string.h>
#include <symbols.h>

/* Real owners are included so each test can reset their private boot state. */
#include "../../src/lib/imd_cbmem.c"
#include "../../src/lib/bootmem.c"
#include "../../src/lib/bmp_logo.c"
#define main coreboot_main
#include "../../src/lib/render_bmp.c"
#undef main

TEST_REGION_UNALLOCATED(program, 0x100000, 0x10000);
TEST_REGION_UNALLOCATED(stack, 0x110000, 0x10000);

static uint8_t cbmem_storage[4 * MiB] __aligned(4096);
static uint8_t source_bitmap[1 * MiB];
static uint32_t pixels[512 * 512];
static size_t source_size;
static unsigned int media_loads, rendered_timestamps;
static bool inject_remove_failure, inject_allocation_failure, wakeup_s3;
static struct lb_framebuffer framebuffer;
static struct resource dram = {
	.base = 0x100000, .size = 0x4000000,
	.flags = IORESOURCE_MEM | IORESOURCE_CACHEABLE | IORESOURCE_ASSIGNED,
};
static struct device domain = {
	.enabled = 1, .path.type = DEVICE_PATH_DOMAIN, .resource_list = &dram,
};
struct device *all_devices = &domain;
struct resource *free_resources;

uintptr_t cbmem_top_chipset(void)
{
	return _cbmem_top_ptr;
}

void cbmem_run_init_hooks(int recovery)
{
}

void bootmem_arch_add_ranges(void)
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

void timestamp_add_now(enum timestamp_id id)
{
	assert_int_equal(id, TS_FIRMWARE_SPLASH_RENDERED);
	rendered_timestamps++;
}

/* Only the CBFS media edge is modeled; its allocator and both owners are real. */
void *_cbfs_alloc(const char *name, cbfs_allocator_t allocator, void *argument,
	size_t *size, bool force_ro, enum cbfs_type *type)
{
	void *buffer;

	assert_string_equal(name, "logo.bmp");
	media_loads++;
	buffer = allocator(argument, source_size, NULL);
	if (!buffer)
		return NULL;
	memcpy(buffer, source_bitmap, source_size);
	*size = source_size;
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
	memranges_teardown(&bootmem);
	memranges_teardown(&bootmem_os);
	initialized = 0;
	table_written = 0;
	aligned_reservation_count = 0;
	aligned_reservations_resolved = false;
	memset(aligned_reservations, 0, sizeof(aligned_reservations));
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
	rendered_timestamps = 0;
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

static void check_retained_map(const struct lb_boot_splash *splash)
{
	struct {
		struct lb_memory header;
		struct lb_memory_range ranges[32];
	} table = { 0 };
	const struct cbmem_entry *entry = cbmem_entry_find(CBMEM_ID_BOOT_SPLASH);
	struct bootmem_aligned_reservation_handle handle;
	struct bootmem_aligned_reservation reservation;
	const struct bootmem_aligned_reservation_request request = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION, .size = sizeof(request),
		.bytes = 196608, .alignment = 4096, .limit_exclusive = 1ULL << 32,
		.tag = BM_MEM_RESERVED,
	};
	uint64_t bmp = splash->bmp_address;
	size_t span = ALIGN_UP(splash->bmp_size, 4096);
	bool reserved = false;

	assert_non_null(entry);
	assert_int_equal(bmp % 4096, 0);
	assert_true(bmp >= (uintptr_t)cbmem_entry_start(entry));
	assert_true(bmp - (uintptr_t)cbmem_entry_start(entry) + span <=
		cbmem_entry_size(entry));
	assert_false(initialized);
	assert_int_equal(bootmem_aligned_reservation_register(&request, &handle), 0);
	table.header.size = sizeof(table.header);
	bootmem_write_memory_table(&table.header);
	assert_int_equal(bootmem_aligned_reservation_query(&handle, &reservation), 0);
	assert_true(bmp + span <= reservation.base ||
		reservation.base + reservation.size <= bmp);
	for (size_t index = 0; index < (table.header.size - sizeof(table.header)) /
	     sizeof(table.ranges[0]); index++) {
		const struct lb_memory_range *range = &table.ranges[index];

		if (bmp >= range->start && bmp + span <= range->start + range->size)
			reserved = range->type == LB_MEM_TABLE;
	}
	assert_true(reserved);
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
		/* Actual early callback, followed by the table fallback: exactly one draw. */
		render_primary_logo(NULL);
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
			assert_int_equal(retained->BitPerPixel, 32);
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
					assert_int_equal(pixels[y * 512 + x], 0xa5a5a5a5);
		bootsplash_render_primary();
		assert_int_equal(media_loads, 1);
		assert_int_equal(rendered_timestamps, 1);
		assert_null(logo_entry);
		check_retained_map(&splash);
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
	check_retained_map(&splash);
}

static void test_retention_failures(void **state)
{
	struct lb_boot_splash splash;

	for (unsigned int mode = 0; mode < 3; mode++) {
		setup(state);
		framebuffer.orientation = LB_FB_ORIENTATION_BOTTOM_UP;
		inject_remove_failure = mode == 0;
		inject_allocation_failure = mode == 1;
		if (mode == 1)
			make_source(300, 4);
		if (mode == 2)
			make_source(512, 512); /* Valid 24bpp source exceeds canonical32 BMP cap. */
		bootsplash_render_primary();
		assert_false(bootsplash_get_handoff(&splash));
		assert_int_equal(rendered_timestamps, 0);
		for (size_t index = 0; index < ARRAY_SIZE(pixels); index++)
			assert_int_equal(pixels[index], 0xa5a5a5a5);
		if (mode != 0)
			assert_null(cbmem_entry_find(CBMEM_ID_BOOT_SPLASH));
	}
}

static void test_retry_and_s3(void **state)
{
	struct lb_boot_splash splash;

	wakeup_s3 = true;
	bootsplash_render_primary();
	assert_false(bootsplash_get_handoff(&splash));
	assert_int_equal(media_loads, 0);
	wakeup_s3 = false;
	framebuffer.physical_address = 0;
	render_primary_logo(NULL);
	assert_false(bootsplash_get_handoff(&splash));
	framebuffer.physical_address = (uintptr_t)pixels;
	bootsplash_render_primary();
	assert_true(bootsplash_get_handoff(&splash));
	assert_int_equal(media_loads, 1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(test_rotated_retention, setup),
		cmocka_unit_test_setup(test_offset_small_root, setup),
		cmocka_unit_test_setup(test_retention_failures, setup),
		cmocka_unit_test_setup(test_retry_and_s3, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
