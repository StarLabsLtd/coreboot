/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <bootsplash.h>
#include <cbfs.h>
#include <cbmem.h>
#include <stdint.h>
#include <vendorcode/google/chromeos/chromeos.h>

static const struct cbmem_entry *logo_entry;

struct bmp_cbmem_allocation {
	uint32_t id;
	const struct cbmem_entry *entry;
};

static void *bmp_cbmem_allocator(void *arg, size_t size,
	const union cbfs_mdata *metadata_unused)
{
	struct bmp_cbmem_allocation *allocation = arg;
	size_t allocation_size = size;
	void *buffer;

	(void)metadata_unused;
	if (size == 0 || size > 1 * MiB)
		return NULL;
	if (allocation->id == CBMEM_ID_BOOT_SPLASH)
		allocation_size += DYN_CBMEM_ALIGN_SIZE - 1;
	buffer = cbmem_add(allocation->id, allocation_size);
	allocation->entry = cbmem_entry_find(allocation->id);
	if (buffer && allocation->id == CBMEM_ID_BOOT_SPLASH)
		buffer = (void *)ALIGN_UP((uintptr_t)buffer, DYN_CBMEM_ALIGN_SIZE);
	return buffer;
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
		.id = type == BOOTSPLASH_CENTER &&
			CONFIG(USE_COREBOOT_FOR_BMP_RENDERING) ?
			CBMEM_ID_BOOT_SPLASH : CBMEM_ID_BMP_LOGO,
	};
	logo_buffer = cbfs_alloc(bmp_get_logo_filename(type),
		bmp_cbmem_allocator, &allocation, logo_size);
	logo_entry = allocation.entry;
	if (!logo_buffer) {
		bmp_release_logo();
		return NULL;
	}
	if (!logo_entry) {
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
