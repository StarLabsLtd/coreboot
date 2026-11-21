/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <drivers/efi_variable_service/ipl.c>

static uint8_t firmware[0x500];
static uint8_t guarded[16 + 0x2000 + 16];
static uint8_t *const destination = guarded + 16;
static struct pe_file_header *file;
static struct pe_optional_header *optional;
static struct pe_section_header *section;
static struct pe_relocation_block *relocations;

static int setup(void **state)
{
	struct fv_header *fv = (void *)firmware;
	struct ffs_file_header *ffs = (void *)(firmware + 64);
	struct fv_section_header *fv_section = (void *)(ffs + 1);
	uint8_t *pe = (void *)(fv_section + 1);
	struct dos_header *dos = (void *)pe;
	const uint32_t file_size = sizeof(*ffs) + sizeof(*fv_section) + 0x400;
	const uint32_t section_size = sizeof(*fv_section) + 0x400;

	memset(firmware, 0, sizeof(firmware));
	memset(guarded, 0x5a, sizeof(guarded));
	fv->signature = FV_SIGNATURE;
	fv->length = sizeof(firmware);
	fv->header_length = 64;
	ffs->type = FV_FILETYPE_MM_CORE_STANDALONE;
	for (size_t i = 0; i < 3; i++) {
		ffs->size[i] = file_size >> (8 * i);
		fv_section->size[i] = section_size >> (8 * i);
	}
	fv_section->type = FV_SECTION_PE32;
	dos->magic = DOS_SIGNATURE;
	dos->pe_offset = 64;
	write_le32(pe + 64, PE_SIGNATURE);
	file = (void *)(pe + 68);
	file->machine = CONFIG(ARCH_SMM_X86_64) ? PE_MACHINE_X64 : PE_MACHINE_IA32;
	file->sections = 1;
	file->optional_header_size = sizeof(*optional);
	optional = (void *)(file + 1);
	optional->magic = CONFIG(ARCH_SMM_X86_64) ? PE_OPTIONAL_MAGIC_64 : PE_OPTIONAL_MAGIC_32;
	optional->image_base = CONFIG(ARCH_SMM_X86_64) ? 0x140000000ULL : 0x1000000;
	optional->image_size = 0x2000;
	optional->header_size = 0x200;
	optional->entrypoint = 0x1000;
	optional->directory_count = ARRAY_SIZE(optional->directory);
	optional->directory[PE_DIRECTORY_BASERELOC] = (struct pe_data_directory){0x1100, 12};
	section = (void *)((uint8_t *)optional + sizeof(*optional));
	section->virtual_address = 0x1000;
	section->virtual_size = 0x200;
	section->raw_offset = 0x200;
	section->raw_size = 0x200;
	if (CONFIG(ARCH_SMM_X86_64))
		write_le64(pe + 0x210, optional->image_base + 0x110);
	else
		write_le32(pe + 0x210, optional->image_base + 0x110);
	relocations = (void *)(pe + 0x300);
	relocations->page = 0x1000;
	relocations->size = 12;
	write_le16(relocations + 1,
		   ((CONFIG(ARCH_SMM_X86_64) ? PE_RELOC_DIR64 : PE_RELOC_HIGHLOW) << 12) | 0x10);
	return 0;
}

static void load_image(bool valid)
{
	struct efi_variable_service_image image;
	int result = efi_variable_service_load_mm_core(firmware, sizeof(firmware),
						     destination, 0x2000, &image);

	if (valid) {
		assert_int_equal(result, 0);
		assert_ptr_equal(image.base, destination);
		assert_ptr_equal(image.entry, destination + 0x1000);
		assert_int_equal(image.size, 0x2000);
		uint64_t relocated = CONFIG(ARCH_SMM_X86_64) ?
			read_le64(destination + 0x1010) : read_le32(destination + 0x1010);
		assert_int_equal(relocated, (uintptr_t)destination + 0x110);
	} else {
		assert_int_not_equal(result, 0);
	}
	for (size_t i = 0; i < 16; i++) {
		assert_int_equal(guarded[i], 0x5a);
		assert_int_equal(guarded[16 + 0x2000 + i], 0x5a);
	}
}

static void matching_image(void **state)
{
	load_image(true);
}

static void wrong_architecture(void **state)
{
	file->machine = CONFIG(ARCH_SMM_X86_64) ? PE_MACHINE_IA32 : PE_MACHINE_X64;
	load_image(false);
}

static void wrong_optional_header(void **state)
{
	optional->magic = CONFIG(ARCH_SMM_X86_64) ? PE_OPTIONAL_MAGIC_32 : PE_OPTIONAL_MAGIC_64;
	load_image(false);
}

static void wrong_relocation(void **state)
{
	write_le16(relocations + 1,
		   ((CONFIG(ARCH_SMM_X86_64) ? PE_RELOC_HIGHLOW : PE_RELOC_DIR64) << 12) | 0x10);
	load_image(false);
}

static void overflowing_relocation_page(void **state)
{
	relocations->page = UINT32_MAX;
	load_image(false);
}

static void relocation_crosses_image(void **state)
{
	relocations->page = 0x1fee;
	load_image(false);
}

static void partial_relocation(void **state)
{
	relocations->size = 11;
	load_image(false);
}

static void section_exceeds_image(void **state)
{
	section->virtual_address = 0x1fff;
	load_image(false);
}

static void short_optional_header(void **state)
{
	file->optional_header_size--;
	load_image(false);
}

static void destination_crosses_4g(void **state)
{
	struct efi_variable_service_image image;

	assert_int_not_equal(efi_variable_service_load_mm_core(firmware, sizeof(firmware),
		(void *)(uintptr_t)0xfffffff0, 0x2000, &image), 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(matching_image, setup),
		cmocka_unit_test_setup(wrong_architecture, setup),
		cmocka_unit_test_setup(wrong_optional_header, setup),
		cmocka_unit_test_setup(wrong_relocation, setup),
		cmocka_unit_test_setup(overflowing_relocation_page, setup),
		cmocka_unit_test_setup(relocation_crosses_image, setup),
		cmocka_unit_test_setup(partial_relocation, setup),
		cmocka_unit_test_setup(section_exceeds_image, setup),
		cmocka_unit_test_setup(short_optional_header, setup),
		cmocka_unit_test_setup(destination_crosses_4g, setup),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
