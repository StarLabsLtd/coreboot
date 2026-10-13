/* SPDX-License-Identifier: GPL-2.0-only */

#include "ipl.h"

#include <commonlib/bsd/helpers.h>
#include <commonlib/endian.h>
#include <string.h>

#define FV_SIGNATURE			0x4856465f
#define FV_FILETYPE_MM_CORE_STANDALONE	0x0f
#define FV_FILETYPE_MAX			0xff
#define FV_SECTION_PE32			0x10

#define DOS_SIGNATURE			0x5a4d
#define PE_SIGNATURE			0x00004550
#define PE_MACHINE_IA32			0x014c
#define PE_OPTIONAL_MAGIC_32		0x010b
#define PE_DIRECTORY_BASERELOC		5
#define PE_RELOC_ABSOLUTE		0
#define PE_RELOC_HIGHLOW		3

struct fv_header {
	uint8_t zero_vector[16];
	uint8_t file_system_guid[16];
	uint64_t length;
	uint32_t signature;
	uint32_t attributes;
	uint16_t header_length;
	uint16_t checksum;
	uint16_t ext_header_offset;
	uint8_t reserved;
	uint8_t revision;
} __packed;

struct ffs_file_header {
	uint8_t name[16];
	uint8_t integrity_check[2];
	uint8_t type;
	uint8_t attributes;
	uint8_t size[3];
	uint8_t state;
} __packed;

struct fv_section_header {
	uint8_t size[3];
	uint8_t type;
} __packed;

struct dos_header {
	uint16_t magic;
	uint8_t unused[58];
	uint32_t pe_offset;
} __packed;

struct pe_file_header {
	uint16_t machine;
	uint16_t sections;
	uint32_t timestamp;
	uint32_t symbol_table;
	uint32_t symbols;
	uint16_t optional_header_size;
	uint16_t characteristics;
} __packed;

struct pe_data_directory {
	uint32_t address;
	uint32_t size;
} __packed;

struct pe_optional_header32 {
	uint16_t magic;
	uint8_t linker_major;
	uint8_t linker_minor;
	uint32_t code_size;
	uint32_t initialized_data_size;
	uint32_t uninitialized_data_size;
	uint32_t entrypoint;
	uint32_t code_base;
	uint32_t data_base;
	uint32_t image_base;
	uint32_t section_alignment;
	uint32_t file_alignment;
	uint16_t os_major;
	uint16_t os_minor;
	uint16_t image_major;
	uint16_t image_minor;
	uint16_t subsystem_major;
	uint16_t subsystem_minor;
	uint32_t win32_version;
	uint32_t image_size;
	uint32_t header_size;
	uint32_t checksum;
	uint16_t subsystem;
	uint16_t dll_characteristics;
	uint32_t stack_reserve;
	uint32_t stack_commit;
	uint32_t heap_reserve;
	uint32_t heap_commit;
	uint32_t loader_flags;
	uint32_t directory_count;
	struct pe_data_directory directory[16];
} __packed;

struct pe_section_header {
	uint8_t name[8];
	uint32_t virtual_size;
	uint32_t virtual_address;
	uint32_t raw_size;
	uint32_t raw_offset;
	uint32_t relocations;
	uint32_t line_numbers;
	uint16_t relocation_count;
	uint16_t line_number_count;
	uint32_t characteristics;
} __packed;

struct pe_relocation_block {
	uint32_t page;
	uint32_t size;
} __packed;

static size_t size24(const uint8_t size[3])
{
	return size[0] | ((size_t)size[1] << 8) | ((size_t)size[2] << 16);
}

static bool range_fits(size_t offset, size_t length, size_t total)
{
	return offset <= total && length <= total - offset;
}

static int find_mm_core(const void *fv, size_t fv_size, const void **pe, size_t *pe_size,
			uint8_t file_guid[16])
{
	const uint8_t *bytes = fv;
	const struct fv_header *header;
	size_t offset;
	size_t length;

	if (fv_size < sizeof(*header))
		return -1;
	header = fv;
	if (read_le32(&header->signature) != FV_SIGNATURE)
		return -1;
	length = read_le64(&header->length);
	if (length > fv_size || read_le16(&header->header_length) > length)
		return -1;
	offset = ALIGN_UP(read_le16(&header->header_length), 8);

	while (range_fits(offset, sizeof(struct ffs_file_header), length)) {
		const struct ffs_file_header *file = (const void *)(bytes + offset);
		size_t file_size = size24(file->size);
		size_t section_offset;

		if (file->type == FV_FILETYPE_MAX)
			break;
		if (file_size < sizeof(*file) || !range_fits(offset, file_size, length))
			return -1;
		if (file->type != FV_FILETYPE_MM_CORE_STANDALONE) {
			offset = ALIGN_UP(offset + file_size, 8);
			continue;
		}

		section_offset = offset + sizeof(*file);
		while (range_fits(section_offset, sizeof(struct fv_section_header),
				  offset + file_size)) {
			const struct fv_section_header *section =
				(const void *)(bytes + section_offset);
			size_t section_size = size24(section->size);

			if (section_size < sizeof(*section) ||
			    !range_fits(section_offset, section_size, offset + file_size))
				return -1;
			if (section->type == FV_SECTION_PE32) {
				memcpy(file_guid, file->name, 16);
				*pe = bytes + section_offset + sizeof(*section);
				*pe_size = section_size - sizeof(*section);
				return 0;
			}
			section_offset = ALIGN_UP(section_offset + section_size, 4);
		}
		return -1;
	}

	return -1;
}

static int apply_relocations(uint8_t *image, size_t image_size,
			     const struct pe_optional_header32 *optional)
{
	const struct pe_data_directory *directory;
	uint32_t image_base = read_le32(&optional->image_base);
	uint32_t loaded_base = (uintptr_t)image;
	uint32_t delta = loaded_base - image_base;
	size_t offset;
	size_t end;

	if (read_le32(&optional->directory_count) <= PE_DIRECTORY_BASERELOC)
		return delta ? -1 : 0;
	directory = &optional->directory[PE_DIRECTORY_BASERELOC];
	if (!read_le32(&directory->size))
		return delta ? -1 : 0;
	offset = read_le32(&directory->address);
	if (!range_fits(offset, read_le32(&directory->size), image_size))
		return -1;
	end = offset + read_le32(&directory->size);

	while (offset < end) {
		const struct pe_relocation_block *block;
		size_t entries;

		if (!range_fits(offset, sizeof(*block), end))
			return -1;
		block = (const void *)(image + offset);
		if (read_le32(&block->size) < sizeof(*block) ||
		    !range_fits(offset, read_le32(&block->size), end))
			return -1;
		entries = (read_le32(&block->size) - sizeof(*block)) / sizeof(uint16_t);
		const uint16_t *entry = (const void *)(block + 1);

		for (size_t i = 0; i < entries; i++) {
			uint16_t relocation = read_le16(&entry[i]);
			unsigned int type = relocation >> 12;
			size_t target = read_le32(&block->page) + (relocation & 0xfff);

			if (type == PE_RELOC_ABSOLUTE)
				continue;
			if (type != PE_RELOC_HIGHLOW ||
			    !range_fits(target, sizeof(uint32_t), image_size))
				return -1;
			write_le32(image + target, read_le32(image + target) + delta);
		}
		offset += read_le32(&block->size);
	}

	return 0;
}

int efi_variable_service_load_mm_core(const void *fv, size_t fv_size,
				      void *destination, size_t destination_size,
				      struct efi_variable_service_image *image)
{
	const uint8_t *pe;
	const struct dos_header *dos;
	const struct pe_file_header *file;
	const struct pe_optional_header32 *optional;
	const struct pe_section_header *section;
	size_t pe_size;
	size_t headers_end;
	size_t image_size;

	if (!fv || !destination || !image || (uintptr_t)destination > UINT32_MAX ||
	    find_mm_core(fv, fv_size, (const void **)&pe, &pe_size, image->file_guid) ||
	    pe_size < sizeof(*dos))
		return -1;
	dos = (const void *)pe;
	if (read_le16(&dos->magic) != DOS_SIGNATURE ||
	    !range_fits(read_le32(&dos->pe_offset),
			sizeof(uint32_t) + sizeof(*file), pe_size))
		return -1;
	if (read_le32(pe + read_le32(&dos->pe_offset)) != PE_SIGNATURE)
		return -1;
	file = (const void *)(pe + read_le32(&dos->pe_offset) + sizeof(uint32_t));
	if (read_le16(&file->machine) != PE_MACHINE_IA32 ||
	    read_le16(&file->optional_header_size) < sizeof(*optional))
		return -1;
	optional = (const void *)(file + 1);
	if (!range_fits((const uint8_t *)optional - pe,
			read_le16(&file->optional_header_size), pe_size) ||
	    read_le16(&optional->magic) != PE_OPTIONAL_MAGIC_32)
		return -1;
	section = (const void *)((const uint8_t *)optional +
				 read_le16(&file->optional_header_size));
	headers_end = (const uint8_t *)section - pe +
		      read_le16(&file->sections) * sizeof(*section);
	image_size = read_le32(&optional->image_size);
	if (headers_end > pe_size || image_size > destination_size ||
	    read_le32(&optional->header_size) > pe_size ||
	    read_le32(&optional->header_size) > image_size ||
	    read_le32(&optional->entrypoint) >= image_size)
		return -1;

	memset(destination, 0, image_size);
	memcpy(destination, pe, read_le32(&optional->header_size));
	for (size_t i = 0; i < read_le16(&file->sections); i++) {
		size_t source = read_le32(&section[i].raw_offset);
		size_t target = read_le32(&section[i].virtual_address);
		size_t raw_size = read_le32(&section[i].raw_size);
		size_t virtual_size = read_le32(&section[i].virtual_size);

		if (!range_fits(source, raw_size, pe_size) ||
		    !range_fits(target, MAX(raw_size, virtual_size), image_size))
			return -1;
		memcpy((uint8_t *)destination + target, pe + source, raw_size);
	}
	if (apply_relocations(destination, image_size, optional))
		return -1;

	image->base = destination;
	image->size = image_size;
	image->entry = (void *)((uint8_t *)destination + read_le32(&optional->entrypoint));
	return 0;
}
