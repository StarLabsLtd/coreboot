/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/acpimmio.h>
#include <soc/southbridge.h>
#include <tests/test.h>

static uint16_t timer_port;
static uint8_t timer_decode;

static uint16_t mock_pm_read16(uint8_t reg)
{
	assert_int_equal(reg, PM_TMR_BLK);
	return timer_port;
}

static uint8_t mock_pm_read8(uint8_t reg)
{
	assert_int_equal(reg, PM_ACPI_CONF);
	return timer_decode;
}

#define pm_read16 mock_pm_read16
#define pm_read8 mock_pm_read8
#include <drivers/efi_variable_service/ramstage_amd.c>
#include <drivers/efi_variable_service/platform_amd.c>

static struct region_device device;
static bool writer_ready, silicon_ram, final_ram;
static uintptr_t ram_top, smram_start;
static size_t smram_length;
static uint8_t image[EFI_CAPSULE_BLOCK_SIZE], scratch[EFI_CAPSULE_BLOCK_SIZE];
static uint8_t flash[EFI_CAPSULE_BLOCK_SIZE];
static unsigned int reads, writes, erases;

const struct region_device *psp_rom_armor_smm_device(void)
{
	return writer_ready ? &device : NULL;
}

uintptr_t cbmem_top(void) { return ram_top; }
void smm_region(uintptr_t *base, size_t *size)
{
	*base = smram_start;
	*size = smram_length;
}

bool opensil_memory_region_is_ram(uint64_t base, uint64_t size)
{
	assert_int_equal(base, MiB);
	assert_int_equal(size, 255 * MiB);
	return silicon_ram;
}

int bootmem_region_targets_type(uint64_t base, uint64_t size, enum bootmem_type type)
{
	assert_int_equal(base, MiB);
	assert_int_equal(size, 255 * MiB);
	assert_int_equal(type, BM_MEM_RAM);
	return final_ram;
}

static size_t sector_offset(size_t offset, size_t size)
{
	assert_int_equal(size, 4 * KiB);
	assert_true(offset >= 0x801000 && offset < 0x810000);
	return offset - 0x800000;
}

static ssize_t read_sector(const struct region_device *rdev, void *out, size_t offset, size_t size)
{
	reads++;
	memcpy(out, flash + sector_offset(offset, size), size);
	return size;
}

static ssize_t write_sector(const struct region_device *rdev, const void *in,
			    size_t offset, size_t size)
{
	writes++;
	memcpy(flash + sector_offset(offset, size), in, size);
	return size;
}

static ssize_t erase_sector(const struct region_device *rdev, size_t offset, size_t size)
{
	erases++;
	memset(flash + sector_offset(offset, size), 0xff, size);
	return size;
}

static const struct region_device_ops ops = {
	.readat = read_sector, .writeat = write_sector, .eraseat = erase_sector,
};

static int setup(void **unused)
{
	region_device_init(&device, &ops, 0, CONFIG_ROM_SIZE);
	writer_ready = silicon_ram = final_ram = true;
	ram_top = smram_start = 0x90000000;
	smram_length = 8 * MiB;
	timer_port = ACPI_PM_TMR_BLK;
	timer_decode = PM_ACPI_DECODE_STD | PM_ACPI_TIMER_EN_EN;
	reads = writes = erases = 0;
	memset(image, 0x55, sizeof(image));
	memset(flash, 0xa5, sizeof(flash));
	return 0;
}

static void private_platform_handoff(void **unused)
{
	struct efi_variable_service_state state = { 0 };

	assert_int_equal(efi_variable_service_ram_end(), 256 * MiB);
	assert_int_equal(efi_variable_service_bind_spi(&state), 0);
	state.spi_bar = 1;
	assert_int_equal(efi_variable_service_bind_spi(&state), -1);
	assert_ptr_equal(efi_variable_service_write_device(), &device);
	writer_ready = false;
	assert_null(efi_variable_service_write_device());
}

static void reject_bad_timer_or_memory(void **unused)
{

	timer_port = 0;
	assert_int_equal(efi_variable_service_ram_end(), 0);
	timer_port = ACPI_PM_TMR_BLK;
	timer_decode = PM_ACPI_DECODE_STD;
	assert_int_equal(efi_variable_service_ram_end(), 0);
	timer_decode |= PM_ACPI_TIMER_EN_EN;
	silicon_ram = false;
	assert_int_equal(efi_variable_service_ram_end(), 0);
	silicon_ram = true;
	ram_top = 128 * MiB;
	assert_int_equal(efi_variable_service_ram_end(), 0);
	ram_top = 0x90000000;
	smram_start = 128 * MiB;
	assert_int_equal(efi_variable_service_ram_end(), 0);
	smram_start = ram_top;
	smram_length = 0;
	assert_int_equal(efi_variable_service_ram_end(), 0);
}

static void final_reservations_checked(void **unused)
{
	assert_true(efi_variable_service_ram_ready());
	final_ram = false;
	assert_false(efi_variable_service_ram_ready());
}

static void capsule_policy_and_shared_block(void **unused)
{
	const struct efi_capsule_range coreboot = { 0x801000, 0x79f000 };
	const struct efi_capsule_range forbidden[] = {
		{ 0, CONFIG_ROM_SIZE }, { 0x760000, 0x80000 }, { 0x800000, 0x1000 },
		{ 0xfa0000, 0x20000 }, { 0xfc0000, 0x40000 },
	};

	for (size_t i = 0; i < ARRAY_SIZE(forbidden); i++)
		assert_int_equal(efi_variable_service_program_capsule(&device, 0x800000,
			 &forbidden[i], 1, image, scratch, true), -1);
	assert_int_equal(efi_variable_service_program_capsule(&device, 0x800000,
			 &coreboot, 1, image, scratch, true), 0);
	assert_int_equal(reads + writes + erases, 0);
	assert_int_equal(efi_variable_service_program_capsule(&device, 0x800000,
			 &coreboot, 1, image, scratch, false), 0);
	assert_int_equal(erases, 15);
	for (size_t i = 0; i < sizeof(flash); i++)
		assert_int_equal(flash[i], i < 0x1000 ? 0xa5 : 0x55);
	writer_ready = false;
	assert_int_equal(efi_variable_service_program_capsule(&device, 0x800000,
			 &coreboot, 1, image, scratch, true), -1);
	assert_int_equal(erases, 15);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(private_platform_handoff, setup),
		cmocka_unit_test_setup(reject_bad_timer_or_memory, setup),
		cmocka_unit_test_setup(final_reservations_checked, setup),
		cmocka_unit_test_setup(capsule_policy_and_shared_block, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
