/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <drivers/efi_variable_service/hob.c>

static uint8_t memory[128 * KiB] __aligned(0x1000);
static struct efi_variable_service_platform platform;
static bool platform_error;

int efi_variable_service_get_platform_info(struct efi_variable_service_platform *info)
{
	*info = platform;
	return platform_error ? -1 : 0;
}

int smmstore_get_info(struct smmstore_params_info *info)
{
	*info = (struct smmstore_params_info) {
		.num_blocks = 8, .block_size = 64 * KiB, .mmap_addr = 0xff000000,
	};
	return 0;
}

int fmap_locate_area(const char *name, struct region *region)
{
	assert_string_equal(name, "SMMSTORE");
	*region = region_create(0, 512 * KiB);
	return 0;
}

void efi_fw_versions(uint32_t *version, uint32_t *lowest)
{
	*version = 42;
	*lowest = 10;
}

static int build_hobs(size_t space)
{
	struct efi_variable_service_image image = {
		.base = memory + 0x3000, .size = 0x1000,
		.entry = (void *)(memory + 0x3000),
	};
	uintptr_t base = (uintptr_t)memory + 0x4000;

	return efi_variable_service_build_hobs(base, base + space,
			(uintptr_t)memory + 0x1000, 0x2000, &image,
			0x10000000, EFI_VARIABLE_SERVICE_BUFFER_SIZE, (void *)memory);
}

static int setup(void **unused)
{
	memset(memory, 0, sizeof(memory));
	platform_error = false;
	platform = (struct efi_variable_service_platform) {
		.runtime_ram = region_create(1 * MiB, 255 * MiB),
		.pm_timer = 0x408,
	};
	return 0;
}

static void check_hobs(bool intel_spi)
{
	struct efi_variable_service_state *state = (void *)memory;
	unsigned int spi_count = 0, timer_count = 0, ram_count = 0;
	unsigned int spi_config_count = 0;
	uintptr_t cursor;

	assert_int_equal(build_hobs(sizeof(memory) - 0x4000), 0);
	for (cursor = state->hob_base; cursor < state->hob_base + state->hob_size; ) {
		struct hob_header *header = (void *)cursor;

		assert_true(header->length >= sizeof(*header));
		assert_true(header->length <= state->hob_base + state->hob_size - cursor);
		if (header->type == HOB_RESOURCE_DESCRIPTOR) {
			struct hob_resource *resource = (void *)header;

			if (!memcmp(&resource->owner, &capsule_ram_guid, sizeof(capsule_ram_guid))) {
				assert_int_equal(resource->type, RESOURCE_SYSTEM_MEMORY);
				assert_int_equal(resource->base, region_offset(&platform.runtime_ram));
				assert_int_equal(resource->size, region_sz(&platform.runtime_ram));
				ram_count++;
			}
			if (intel_spi && resource->base == platform.spi_config) {
				assert_int_equal(resource->size, 0x1000);
				spi_config_count++;
			}
		} else if (header->type == HOB_GUID_EXTENSION) {
			struct hob_guid *guid = (void *)header;

			if (!memcmp(&guid->name, &spi_flash_info_guid, sizeof(guid->name))) {
				struct spi_flash_info_hob *spi = (void *)(guid + 1);

				assert_int_equal(spi->spi_address.address, platform.spi_config);
				assert_int_equal(spi->flags, platform.spi_flags);
				spi_count++;
			} else if (!memcmp(&guid->name, &acpi_board_info_guid, sizeof(guid->name))) {
				struct acpi_board_info_hob *acpi = (void *)(guid + 1);

				assert_int_equal(acpi->pm_timer_base, platform.pm_timer);
				timer_count++;
			}
		}
		cursor += header->length;
		if (header->type == HOB_END)
			break;
	}
	assert_int_equal(cursor, state->hob_base + state->hob_size);
	assert_int_equal(spi_count, intel_spi);
	assert_int_equal(spi_config_count, intel_spi);
	assert_int_equal(ram_count, 1);
	assert_int_equal(timer_count, 1);
	assert_int_equal(state->spi_mmio_resource != 0, intel_spi);
	assert_int_equal(state->capsule_info != 0, CONFIG(DRIVERS_EFI_UPDATE_CAPSULES));
	if (CONFIG(DRIVERS_EFI_UPDATE_CAPSULES)) {
		struct capsule_info_hob *capsule = (void *)(uintptr_t)state->capsule_info;

		assert_int_equal(capsule->revision, 1);
		assert_int_equal(capsule->size, sizeof(*capsule));
		assert_int_equal(capsule->flash_size, CONFIG_ROM_SIZE);
		assert_int_equal(capsule->lowest_supported_version, 10);
	}
	if (intel_spi) {
		struct hob_resource *bar = (void *)(uintptr_t)state->spi_mmio_resource;

		assert_int_equal(bar->base, 0);
		assert_int_equal(bar->size, 0x1000);
		assert_int_equal(bar->type, RESOURCE_MEMORY_MAPPED_IO);
	}
}

static void private_callbacks_only(void **unused)
{
	check_hobs(false);
}

static void intel_spi_descriptors(void **unused)
{
	platform.spi_config = 0xe01fd000;
	platform.spi_flags = SPI_DISABLE_SMM_WRITE_PROTECT;
	check_hobs(true);
}

static void refuse_invalid_platform(void **unused)
{
	platform_error = true;
	assert_int_equal(build_hobs(sizeof(memory) - 0x4000), -1);
	platform_error = false;
	platform.pm_timer = 0;
	assert_int_equal(build_hobs(sizeof(memory) - 0x4000), -1);
}

static void refuse_invalid_ram(void **unused)
{
	platform.runtime_ram = region_create(0, MiB);
	assert_int_equal(build_hobs(sizeof(memory) - 0x4000), -1);
	platform.runtime_ram = region_create(0xffff0000, 0x20000);
	assert_int_equal(build_hobs(sizeof(memory) - 0x4000), -1);
}

static void refuse_exhausted_hob_area(void **unused)
{
	assert_int_equal(build_hobs(sizeof(struct hob_handoff)), -1);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(private_callbacks_only, setup),
		cmocka_unit_test_setup(intel_spi_descriptors, setup),
		cmocka_unit_test_setup(refuse_invalid_platform, setup),
		cmocka_unit_test_setup(refuse_invalid_ram, setup),
		cmocka_unit_test_setup(refuse_exhausted_hob_area, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
