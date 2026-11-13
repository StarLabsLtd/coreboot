/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
/* The IA32 MM entry points are not executed by this host-side test. */
#if defined(__x86_64__)
#define cdecl sysv_abi
#endif
#include <drivers/efi_variable_service/smi.c>
#undef cdecl

static struct region_device flash_device;
static uint8_t flash[CONFIG_ROM_SIZE];
static uint8_t image[EFI_CAPSULE_BLOCK_SIZE];
static bool write_enabled;
static unsigned int reads, writes, erases;

static ssize_t flash_read(const struct region_device *rdev, void *buffer,
			  size_t offset, size_t size)
{
	reads++;
	memcpy(buffer, flash + offset, size);
	return size;
}

static ssize_t flash_write(const struct region_device *rdev, const void *buffer,
			   size_t offset, size_t size)
{
	writes++;
	for (size_t i = 0; i < size; i++)
		flash[offset + i] &= ((const uint8_t *)buffer)[i];
	return size;
}

static ssize_t flash_erase(const struct region_device *rdev, size_t offset, size_t size)
{
	erases++;
	memset(flash + offset, 0xff, size);
	return size;
}

static const struct region_device_ops flash_ops = {
	.readat = flash_read,
	.writeat = flash_write,
	.eraseat = flash_erase,
};

const struct region_device *boot_device_ro(void)
{
	return &flash_device;
}

const struct region_device *boot_device_rw(void)
{
	return &flash_device;
}

bool fast_spi_wpd_status(void)
{
	return write_enabled;
}

size_t fast_spi_get_bios_region(size_t *size)
{
	*size = 0x1000000;
	return 0x1000000;
}

static int setup(void **state)
{
	region_device_init(&flash_device, &flash_ops, 0, sizeof(flash));
	memset(flash, 0xa5, sizeof(flash));
	memset(image, 0, sizeof(image));
	write_enabled = true;
	reads = writes = erases = 0;
	return 0;
}

static void pin_and_variable_store_preserved(void **state)
{
	const struct efi_capsule_range bios = { .offset = 0x1000000, .size = 0x1000000 };
	const uint32_t offsets[] = { FMAP_SECTION_SRK_PIN_V1_START,
		FMAP_SECTION_SMMSTORE_START,
		FMAP_SECTION_SMMSTORE_START + FMAP_SECTION_SMMSTORE_SIZE - sizeof(image) };

	for (size_t i = 0; i < ARRAY_SIZE(offsets); i++) {
		for (unsigned int check_only = 0; check_only <= 1; check_only++)
			assert_int_equal(capsule_program(offsets[i], &bios, 1, image, check_only),
					 EFI_VARIABLE_SERVICE_RET_SUCCESS);
		for (size_t j = 0; j < sizeof(image); j++)
			assert_int_equal(flash[offsets[i] + j], 0xa5);
	}
	assert_int_equal(reads + writes + erases, 0);
}

static void fmap_preflight(void **state)
{
	const struct efi_capsule_range fmap = {
		.offset = FMAP_SECTION_FMAP_START, .size = FMAP_SECTION_FMAP_SIZE,
	};

	memcpy(image, flash + fmap.offset, sizeof(image));
	assert_int_equal(capsule_program(fmap.offset, &fmap, 1, image, 1),
			 EFI_VARIABLE_SERVICE_RET_SUCCESS);
	/* Both kinds of bit changes must fail before any erase or programming. */
	for (unsigned int value = 0; value <= 0xff; value += 0xff) {
		image[0] = value;
		for (unsigned int check_only = 0; check_only <= 1; check_only++)
			assert_int_equal(capsule_program(fmap.offset, &fmap, 1, image, check_only),
					 EFI_VARIABLE_SERVICE_RET_FAILURE);
	}
	assert_int_equal(writes + erases, 0);
	assert_int_equal(flash[fmap.offset], 0xa5);
}

static void ro_cbfs_has_separate_erase_block(void **state)
{
	const struct efi_capsule_range cbfs = {
		.offset = FMAP_SECTION_COREBOOT_START, .size = 1,
	};
	const size_t block = ALIGN_DOWN(cbfs.offset, sizeof(image));

	/* Updating CBFS must not inherit FMAP's no-erase restriction. */
	memset(image, 0xff, sizeof(image));
	assert_int_equal(capsule_program(block, &cbfs, 1, image, 1),
			 EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(writes + erases, 0);
	assert_int_equal(capsule_program(block, &cbfs, 1, image, 0),
			 EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(writes, 1);
	assert_int_equal(erases, 1);
	for (size_t i = 0; i < sizeof(image); i++)
		assert_int_equal(flash[block + i], block + i == cbfs.offset ? 0xff : 0xa5);
	assert_int_equal(flash[FMAP_SECTION_FMAP_START], 0xa5);
	assert_int_equal(flash[FMAP_SECTION_SRK_PIN_V1_START], 0xa5);
}

static void pin_read_and_resolution(void **state)
{
	static const char pin_name[16] = "SRK_PIN_V1", fmap_name[16] = "FMAP";
	struct efi_capsule_range range;

	assert_int_equal(capsule_read(FMAP_SECTION_SRK_PIN_V1_START - 1, 2),
			 EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(capsule_read(FMAP_SECTION_SRK_PIN_V1_START, 1),
			 EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(reads, 0);
	assert_int_equal(capsule_read(FMAP_SECTION_SRK_PIN_V1_START +
				     FMAP_SECTION_SRK_PIN_V1_SIZE, 1),
			 EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(capsule_resolve(pin_name, &range), EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(capsule_resolve(fmap_name, &range), EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(range.offset, FMAP_SECTION_FMAP_START);
	assert_int_equal(range.size, FMAP_SECTION_FMAP_SIZE);
}

static void missing_write_protection(void **state)
{
	const struct efi_capsule_range bios = { .offset = 0x1000000, .size = 0x1000000 };

	write_enabled = false;
	assert_int_equal(capsule_program(FMAP_SECTION_SRK_PIN_V1_START, &bios, 1, image, 0),
			 EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(capsule_program(FMAP_SECTION_FMAP_START, &bios, 1, image, 1),
			 EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(reads + writes + erases, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(pin_and_variable_store_preserved, setup),
		cmocka_unit_test_setup(fmap_preflight, setup),
		cmocka_unit_test_setup(ro_cbfs_has_separate_erase_block, setup),
		cmocka_unit_test_setup(pin_read_and_resolution, setup),
		cmocka_unit_test_setup(missing_write_protection, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
