/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <bootsplash.h>
#include <cbfs.h>
#include <cbmem.h>
#include <stdint.h>
#include <string.h>

#include "render_bmp.h"
#include <vendorcode/google/chromeos/chromeos.h>

static const struct cbmem_entry *logo_entry;

struct bmp_cbmem_allocation {
	uint32_t id;
	const struct cbmem_entry *entry;
	bool decoded;
};

static void *bmp_cbmem_allocator(void *argument, size_t size,
	const union cbfs_mdata *metadata)
{
	struct bmp_cbmem_allocation *allocation = argument;
	size_t requested = size;
	uintptr_t address, original;
	void *buffer;
	uint64_t extent;

	(void)metadata;
	if (!size || size > BMP_LOGO_MAX_SIZE)
		return NULL;
	if (allocation->id == CBMEM_ID_BOOT_SPLASH) {
		if (size > SIZE_MAX - (DYN_CBMEM_ALIGN_SIZE - 1U))
			return NULL;
		requested = ALIGN_UP(size, DYN_CBMEM_ALIGN_SIZE);
		if (requested > SIZE_MAX - (DYN_CBMEM_ALIGN_SIZE - 1U))
			return NULL;
		requested += DYN_CBMEM_ALIGN_SIZE - 1U;
	}
	buffer = cbmem_add(allocation->id, requested);
	allocation->entry = cbmem_entry_find(allocation->id);
	if (!buffer || !allocation->entry)
		return NULL;
	original = (uintptr_t)buffer;
	extent = cbmem_entry_size(allocation->entry);
	if (cbmem_entry_start(allocation->entry) != buffer || extent < requested)
		return NULL;
	address = original;
	if (allocation->id == CBMEM_ID_BOOT_SPLASH) {
		if (original > UINTPTR_MAX - (DYN_CBMEM_ALIGN_SIZE - 1U))
			return NULL;
		address = ALIGN_UP(original, DYN_CBMEM_ALIGN_SIZE);
		if (address - original > extent ||
		    ALIGN_UP(size, DYN_CBMEM_ALIGN_SIZE) > extent - (address - original))
			return NULL;
	}
	if ((allocation->id == CBMEM_ID_BOOT_SPLASH ?
	    ALIGN_UP(size, DYN_CBMEM_ALIGN_SIZE) : size) - 1U > UINTPTR_MAX - address)
		return NULL;
	return (void *)address;
}

/* Mapping of different bootsplash logo name based on bootsplash type */
static const char *bootsplash_list[BOOTSPLASH_MAX_NUM] = {
	[BOOTSPLASH_LOW_BATTERY] = "low_battery.bmp",
	[BOOTSPLASH_CENTER] = "logo.bmp",
	[BOOTSPLASH_FOOTER] = "footer_logo.bmp",
	[BOOTSPLASH_OFF_MODE_CHARGING] = "off_mode_charging.bmp"
};

/* Mapping of different bootsplash logo name (including secondary) based on bootsplash type */
static const char *alt_bootsplash_list[BOOTSPLASH_MAX_NUM] = {
	[BOOTSPLASH_LOW_BATTERY] = "low_battery_alt.bmp",
	[BOOTSPLASH_CENTER] = "logo.bmp",
	[BOOTSPLASH_FOOTER] = "footer_logo.bmp",
	[BOOTSPLASH_OFF_MODE_CHARGING] = "off_mode_charging_alt.bmp"
};

/*
 * Return the appropriate logo filename based on the bootsplash type.
 * This will be the default filename from 'bootsplash_list' or the
 * custom one if it was overridden for BOOTSPLASH_OEM.
 */
static const char *bmp_get_logo_filename(enum bootsplash_type type)
{
	/* Override `BOOTSPLASH_CENTER` logo name if required */
	if ((type == BOOTSPLASH_CENTER) && CONFIG(HAVE_CUSTOM_BMP_LOGO))
		return bmp_logo_filename();

	return platform_use_secondary_logo() ? alt_bootsplash_list[type] : bootsplash_list[type];
}

void *bmp_load_logo_by_type(enum bootsplash_type type, size_t *logo_size)
{
	void *logo_buffer;
	struct bmp_cbmem_allocation allocation;

	if (!logo_size)
		return NULL;
	*logo_size = 0;
	if ((unsigned int)type >= BOOTSPLASH_MAX_NUM)
		return NULL;

	/* CBMEM is locked for S3 resume path. */
	if (acpi_is_wakeup_s3())
		return NULL;

	allocation = (struct bmp_cbmem_allocation) {
		.id = type == BOOTSPLASH_CENTER && CONFIG(BOOT_SPLASH_HANDOFF) ?
			CBMEM_ID_BOOT_SPLASH : CBMEM_ID_BMP_LOGO,
	};
	logo_buffer = cbfs_alloc(bmp_get_logo_filename(type), bmp_cbmem_allocator,
		&allocation, logo_size);
	logo_entry = allocation.entry;
	if (!logo_buffer) {
		bmp_release_logo();
		*logo_size = 0;
		return NULL;
	}

	return logo_buffer;
}

void *bmp_load_logo(size_t *logo_size)
{
	enum bootsplash_type type = BOOTSPLASH_CENTER;

	if (platform_is_low_battery_shutdown_needed())
		type = BOOTSPLASH_LOW_BATTERY;

	if (platform_is_off_mode_charging_active())
		type = BOOTSPLASH_OFF_MODE_CHARGING;

	return bmp_load_logo_by_type(type, logo_size);
}

void *bmp_retain_logo_from_blt(const void *blt, uint32_t width, uint32_t height,
	unsigned int scale, size_t *bmp_size)
{
	struct bmp_cbmem_allocation allocation = {
		.id = CBMEM_ID_BOOT_SPLASH,
		.decoded = true,
	};
	struct bmp_image_header *header;
	uint64_t row_size, pixel_bytes, source_size;

	if (!bmp_size)
		return NULL;
	*bmp_size = 0;
	if (!blt || !width || !height || (scale != 1 && scale != 2) ||
	    width > INT32_MAX / scale || height > INT32_MAX / scale)
		return NULL;
	source_size = (uint64_t)width * height * sizeof(struct blt_pixel);
	row_size = ALIGN_UP((uint64_t)width * scale * 3, 4);
	pixel_bytes = row_size * height * scale;
	if (pixel_bytes > BMP_LOGO_MAX_SIZE - sizeof(*header) ||
	    source_size - 1U > UINTPTR_MAX - (uintptr_t)blt)
		return NULL;
	if (logo_entry && cbmem_entry_remove(logo_entry))
		return NULL;
	logo_entry = NULL;
	header = bmp_cbmem_allocator(&allocation, sizeof(*header) + pixel_bytes, NULL);
	logo_entry = allocation.entry;
	if (!header) {
		bmp_release_logo();
		return NULL;
	}
	*header = (struct bmp_image_header) {
		.CharB = 'B', .CharM = 'M', .Size = sizeof(*header) + pixel_bytes,
		.ImageOffset = sizeof(*header), .HeaderSize = 40,
		.PixelWidth = width * scale, .PixelHeight = height * scale,
		.Planes = 1, .BitPerPixel = 24,
		.ImageSize = pixel_bytes,
	};
	memset(header + 1, 0, pixel_bytes);
	for (uint32_t row = 0; row < height; row++) {
		uint8_t *dst = (uint8_t *)(header + 1) + row * scale * row_size;
		const struct blt_pixel *src = (const struct blt_pixel *)blt +
			(height - row - 1U) * width;

		for (uint32_t x = 0; x < width * scale; x++) {
			dst[x * 3] = src[x / scale].Blue;
			dst[x * 3 + 1] = src[x / scale].Green;
			dst[x * 3 + 2] = src[x / scale].Red;
		}
		if (scale == 2)
			memcpy(dst + row_size, dst, row_size);
	}
	*bmp_size = header->Size;
	return header;
}

void bmp_retain_logo(void)
{
	logo_entry = NULL;
}

void bmp_release_logo(void)
{
	if (logo_entry)
		cbmem_entry_remove(logo_entry);
	logo_entry = NULL;
}
