/* SPDX-License-Identifier: GPL-2.0-only */
#include "capsule_policy.h"
#include "capsule_image.h"
#include "capsule.h"
#include <commonlib/bsd/cbfs_serialized.h>
#include <commonlib/helpers.h>
#include <endian.h>
#include <fmap_config.h>
#include <string.h>

#define MSS1_SIGNATURE 0x3153534dU
#define MSS1_HEADER_SIZE 16U
#define DEPENDENCY_LIMIT 4096U
#define DEPENDENCY_STACK_SIZE 64U

#define DEP_PUSH_GUID 0x00U
#define DEP_PUSH_VERSION 0x01U
#define DEP_VERSION_STRING 0x02U
#define DEP_AND 0x03U
#define DEP_OR 0x04U
#define DEP_NOT 0x05U
#define DEP_TRUE 0x06U
#define DEP_FALSE 0x07U
#define DEP_EQUAL 0x08U
#define DEP_GREATER_THAN 0x09U
#define DEP_GREATER_EQUAL 0x0aU
#define DEP_LESS_THAN 0x0bU
#define DEP_LESS_EQUAL 0x0cU
#define DEP_END 0x0dU
#define DEP_DECLARE_LENGTH 0x0eU

#define CBFS_HEADER_SIZE 24U
#define CBFS_MAX_FILES 128U
#define BUILD_INFO_LIMIT 512U


struct dep_value {
	uint32_t value;
	bool boolean;
};

static uint32_t read_le32(const uint8_t *data)
{
	return data[0] | (uint32_t)data[1] << 8 |
		(uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static bool dep_push(struct dep_value stack[], size_t *count, uint32_t value,
	bool boolean)
{
	if (*count == DEPENDENCY_STACK_SIZE)
		return false;
	stack[(*count)++] = (struct dep_value){ value, boolean };
	return true;
}

static bool dep_pop(struct dep_value stack[], size_t *count, bool boolean,
	struct dep_value *value)
{
	if (!*count || stack[*count - 1].boolean != boolean)
		return false;
	*value = stack[--*count];
	return true;
}

static bool dependency(const uint8_t *data, size_t available,
	uint32_t installed_version, const uint8_t image_type[16], size_t *size)
{
	struct dep_value stack[DEPENDENCY_STACK_SIZE];
	struct dep_value first;
	struct dep_value second;
	size_t count = 0;
	size_t offset = 0;
	uint32_t declared_length = 0;

	*size = 0;
	if (available > DEPENDENCY_LIMIT)
		available = DEPENDENCY_LIMIT;
	while (offset < available) {
		uint8_t opcode = data[offset++];
		uint32_t value;

		switch (opcode) {
		case DEP_PUSH_GUID:
			if (sizeof(guid_t) > available - offset ||
			    memcmp(data + offset, image_type,
				sizeof(guid_t)))
				return false;
			offset += sizeof(guid_t);
			if (!dep_push(stack, &count, installed_version, false))
				return false;
			break;
		case DEP_PUSH_VERSION:
			if (sizeof(uint32_t) > available - offset)
				return false;
			value = read_le32(data + offset);
			offset += sizeof(uint32_t);
			if (!dep_push(stack, &count, value, false))
				return false;
			break;
		case DEP_VERSION_STRING:
			while (offset < available && data[offset])
				offset++;
			if (offset == available)
				return false;
			offset++;
			break;
		case DEP_TRUE:
		case DEP_FALSE:
			if (!dep_push(stack, &count, opcode == DEP_TRUE, true))
				return false;
			break;
		case DEP_NOT:
			if (!dep_pop(stack, &count, true, &first) ||
			    !dep_push(stack, &count, !first.value, true))
				return false;
			break;
		case DEP_AND:
		case DEP_OR:
			if (!dep_pop(stack, &count, true, &first) ||
			    !dep_pop(stack, &count, true, &second))
				return false;
			value = opcode == DEP_AND ? first.value && second.value :
				first.value || second.value;
			if (!dep_push(stack, &count, value, true))
				return false;
			break;
		case DEP_EQUAL:
		case DEP_GREATER_THAN:
		case DEP_GREATER_EQUAL:
		case DEP_LESS_THAN:
		case DEP_LESS_EQUAL:
			if (!dep_pop(stack, &count, false, &first) ||
			    !dep_pop(stack, &count, false, &second))
				return false;
			if (opcode == DEP_EQUAL)
				value = first.value == second.value;
			else if (opcode == DEP_GREATER_THAN)
				value = first.value > second.value;
			else if (opcode == DEP_GREATER_EQUAL)
				value = first.value >= second.value;
			else if (opcode == DEP_LESS_THAN)
				value = first.value < second.value;
			else
				value = first.value <= second.value;
			if (!dep_push(stack, &count, value, true))
				return false;
			break;
		case DEP_DECLARE_LENGTH:
			if (offset != 1 || sizeof(uint32_t) > available - offset)
				return false;
			value = read_le32(data + offset);
			offset += sizeof(uint32_t);
			if (!value || value > available ||
			    !dep_push(stack, &count, value, false))
				return false;
			declared_length = value;
			break;
		case DEP_END:
			if (!dep_pop(stack, &count, true, &first) ||
			    !first.value ||
			    (declared_length && declared_length != offset))
				return false;
			*size = offset;
			return true;
		default:
			return false;
		}
	}
	return false;
}
static bool board_matches(struct smm_capsule_image *image, size_t raw_offset,
	const void *fmap, size_t fmap_size)
{
	uint8_t header[256], info[BUILD_INFO_LIMIT];
	size_t offset = 0, files = 0, matches = 0;
	const char expected[] = "\nMAINBOARD_VENDOR: " CONFIG_MAINBOARD_VENDOR
		"\nMAINBOARD_PART_NUMBER: " CONFIG_MAINBOARD_PART_NUMBER "\n";

	/* Layout changes require a programmer installation, not an update. */
	for (size_t done = 0; done < fmap_size;) {
		size_t size = MIN(sizeof(header), fmap_size - done);

		if (!smm_capsule_image_read(image, raw_offset + FMAP_OFFSET + done,
			header, size) || memcmp(header, (const uint8_t *)fmap + done, size))
			return false;
		done += size;
	}
	while (offset + CBFS_HEADER_SIZE <= FMAP_SECTION_COREBOOT_SIZE) {
		uint32_t length, type, attributes, data_offset;
		size_t name_end, next;
		bool empty;

		if (!smm_capsule_image_read(image, raw_offset + FMAP_SECTION_COREBOOT_START +
			offset, header, CBFS_HEADER_SIZE))
			return false;
		if (memcmp(header, "LARCHIVE", 8)) {
			offset += CBFS_ALIGNMENT;
			continue;
		}
		if (++files > CBFS_MAX_FILES)
			return false;
		length = be32dec(header + 8);
		type = be32dec(header + 12);
		attributes = be32dec(header + 16);
		data_offset = be32dec(header + 20);
		empty = type == CBFS_TYPE_DELETED || type == CBFS_TYPE_NULL;
		if (data_offset <= CBFS_HEADER_SIZE || data_offset > sizeof(header) ||
		    (data_offset & 3U) || data_offset > FMAP_SECTION_COREBOOT_SIZE - offset ||
		    length > FMAP_SECTION_COREBOOT_SIZE - offset - data_offset ||
		    !smm_capsule_image_read(image, raw_offset + FMAP_SECTION_COREBOOT_START +
			offset, header, data_offset))
			return false;
		for (name_end = CBFS_HEADER_SIZE; name_end < data_offset && header[name_end];
		     name_end++)
			if (header[name_end] < ' ' || header[name_end] > '~')
				return false;
		if (name_end == data_offset || (name_end == CBFS_HEADER_SIZE && !empty) ||
		    (attributes && ((attributes & 3U) || attributes <= name_end ||
		     attributes >= data_offset)))
			return false;
		if (!empty && name_end - CBFS_HEADER_SIZE == sizeof("build_info") - 1 &&
		    !memcmp(header + CBFS_HEADER_SIZE, "build_info", sizeof("build_info"))) {
			size_t end;

			if (++matches != 1 || type != CBFS_TYPE_RAW ||
			    !length || length > sizeof(info) ||
			    !smm_capsule_image_read(image, raw_offset + FMAP_SECTION_COREBOOT_START +
				offset + data_offset, info, length) || length <= 18 ||
			    memcmp(info, "COREBOOT_VERSION: ", 18))
				return false;
			/* Hash metadata does not transform the signed raw identity data. */
			for (size_t attribute = attributes; attribute && attribute < data_offset;) {
				uint32_t attribute_size;
				size_t digest_size;

				if (data_offset - attribute < 12 ||
				    be32dec(header + attribute) != CBFS_FILE_ATTR_TAG_HASH)
					return false;
				attribute_size = be32dec(header + attribute + 4);
				switch (be32dec(header + attribute + 8)) {
				case VB2_HASH_SHA1: digest_size = VB2_SHA1_DIGEST_SIZE; break;
				case VB2_HASH_SHA256: digest_size = VB2_SHA256_DIGEST_SIZE; break;
				case VB2_HASH_SHA512: digest_size = VB2_SHA512_DIGEST_SIZE; break;
				case VB2_HASH_SHA224: digest_size = VB2_SHA224_DIGEST_SIZE; break;
				case VB2_HASH_SHA384: digest_size = VB2_SHA384_DIGEST_SIZE; break;
				default: return false;
				}
				if (attribute_size != 12 + digest_size ||
				    attribute_size > data_offset - attribute)
					return false;
				attribute += attribute_size;
			}
			for (end = 18; end < length && info[end] != '\n'; end++)
				if (info[end] < ' ' || info[end] > '~')
					return false;
			if (end == 18 || length - end != sizeof(expected) - 1 ||
			    memcmp(info + end, expected, sizeof(expected) - 1))
				return false;
		}
		next = ALIGN_UP(offset + data_offset + length, CBFS_ALIGNMENT);
		if (next <= offset || next > FMAP_SECTION_COREBOOT_SIZE)
			return false;
		offset = next;
	}
	return matches == 1;
}

enum capsule_broker_authentication_status smm_capsule_policy(
	struct smm_capsule_image *image, const uint8_t image_type[16],
	uint32_t current_version, uint32_t lowest_version, uint32_t attempted_version,
	const void *fmap, size_t fmap_size, uint32_t region_count,
	struct capsule_broker_raw_image *raw, uint32_t *selected)
{
	uint8_t metadata[DEPENDENCY_LIMIT + MSS1_HEADER_SIZE];
	uint8_t names[SMM_CAPSULE_MAX_REGIONS][16], trailer[8];
	size_t dependency_size = 0, size = MIN(image->payload_size, sizeof(metadata));
	size_t offset;
	uint32_t header_size, version, floor;
	uint32_t count;

	*raw = (struct capsule_broker_raw_image) { 0 };
	*selected = 0;
	if (size < MSS1_HEADER_SIZE + 1 ||
	    !smm_capsule_image_read(image, image->payload_offset, metadata, size))
		return CAPSULE_BROKER_IMAGE_REFUSED;
	if (read_le32(metadata) != MSS1_SIGNATURE &&
	    !dependency(metadata, size, current_version, image_type, &dependency_size))
		return CAPSULE_BROKER_IMAGE_REFUSED;
	if (dependency_size > size - MSS1_HEADER_SIZE ||
	    read_le32(metadata + dependency_size) != MSS1_SIGNATURE)
		return CAPSULE_BROKER_IMAGE_REFUSED;
	header_size = read_le32(metadata + dependency_size + 4);
	version = read_le32(metadata + dependency_size + 8);
	floor = read_le32(metadata + dependency_size + 12);
	if (header_size < MSS1_HEADER_SIZE ||
	    header_size >= image->payload_size - dependency_size ||
	    version != attempted_version || floor > version)
		return CAPSULE_BROKER_IMAGE_REFUSED;
	if (version < lowest_version)
		return CAPSULE_BROKER_VERSION_REFUSED;
	offset = image->payload_offset + dependency_size + header_size;
	if (!smm_capsule_image_read(image, image->size - sizeof(trailer),
		trailer, sizeof(trailer)) || memcmp(trailer, "RMAP", 4) ||
	    le16dec(trailer + 4) != 1)
		return CAPSULE_BROKER_IMAGE_REFUSED;
	count = le16dec(trailer + 6);
	if (!count || count > region_count || count > ARRAY_SIZE(names) ||
	    image->payload_size - dependency_size - header_size !=
		CONFIG_ROM_SIZE + count * sizeof(names[0]) + sizeof(trailer) ||
	    !smm_capsule_image_read(image, offset + CONFIG_ROM_SIZE, names,
		count * sizeof(names[0])) ||
	    !board_matches(image, offset, fmap, fmap_size))
		return CAPSULE_BROKER_IMAGE_REFUSED;
	for (size_t i = 0; i < count; i++) {
		const char *entry = CONFIG_DRIVERS_EFI_CAPSULE_REGIONS;
		size_t length = 0, region = 0;
		bool found = false;

		while (length < sizeof(names[i]) && names[i][length])
			length++;
		if (!length || length == sizeof(names[i]))
			return CAPSULE_BROKER_IMAGE_REFUSED;
		for (size_t padding = length; padding < sizeof(names[i]); padding++)
			if (names[i][padding])
				return CAPSULE_BROKER_IMAGE_REFUSED;
		/* The compiled list and region array have the same validated order. */
		while (*entry) {
			const char *start;
			size_t word_length;

			while (*entry == ' ' || *entry == '\t')
				entry++;
			if (!*entry)
				break;
			start = entry;
			while (*entry && *entry != ' ' && *entry != '\t')
				entry++;
			word_length = entry - start;
			if (word_length == length && !memcmp(start, names[i], length)) {
				if (region >= region_count || (*selected & (1U << region)))
					return CAPSULE_BROKER_IMAGE_REFUSED;
				*selected |= 1U << region;
				found = true;
				break;
			}
			region++;
		}
		if (!found)
			return CAPSULE_BROKER_IMAGE_REFUSED;
	}
	*raw = (struct capsule_broker_raw_image) {
		.offset = offset, .size = CONFIG_ROM_SIZE, .lowest_supported_version = floor,
	};
	return CAPSULE_BROKER_AUTHENTICATED;
}
