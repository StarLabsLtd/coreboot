/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <payload_mm_fmp_auth_policy.h>
#include <stdint.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM FMP authentication policy must only be built in SMM"
#endif

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

#define FMAP_HEADER_SIZE 56U
#define FMAP_AREA_SIZE 42U
#define FMAP_NAME_SIZE 32U
#define FMAP_MAX_AREAS 64U
#define CBFS_HEADER_SIZE 24U
#define CBFS_ALIGNMENT 64U
#define CBFS_MAX_FILES 128U
#define CBFS_TYPE_RAW 0x50U
#define CBFS_TYPE_DELETED 0U
#define CBFS_TYPE_NULL UINT32_MAX
#define BUILD_INFO_LIMIT 512U

static const uint8_t fmap_signature[] = "__FMAP__";
static const uint8_t cbfs_magic[] = "LARCHIVE";

static struct {
	struct payload_mm_crypto_owner crypto;
	uint8_t trust_xdr[PAYLOAD_MM_MAX_TRUST_XDR_SIZE];
	guid_t image_type;
	char vendor[PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE];
	char part[PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE];
	size_t trust_xdr_size;
	size_t vendor_size;
	size_t part_size;
	uint32_t trusted_lowest_version;
	uint32_t image_size;
	bool installed;
	bool install_attempted;
	bool busy;
} auth_policy;

struct span {
	const uint8_t *data;
	size_t size;
};

struct dep_value {
	uint32_t value;
	bool boolean;
};

static uint16_t read_le16(const uint8_t *data)
{
	return data[0] | (uint16_t)data[1] << 8;
}

static uint32_t read_le32(const uint8_t *data)
{
	return read_le16(data) | (uint32_t)read_le16(data + 2) << 16;
}

static uint64_t read_le64(const uint8_t *data)
{
	return read_le32(data) | (uint64_t)read_le32(data + 4) << 32;
}

static uint32_t read_be32(const uint8_t *data)
{
	return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
		(uint32_t)data[2] << 8 | data[3];
}

static bool bytes_present(const uint8_t *data, size_t size)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < size; i++)
		bits |= data[i];
	return bits != 0;
}

static bool bytes_zero(const uint8_t *data, size_t size)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < size; i++)
		bits |= data[i];
	return bits == 0;
}

static bool text_valid(const char *text, size_t size, size_t maximum)
{
	if (!text || !size || size > maximum)
		return false;
	for (size_t i = 0; i < size; i++)
		if ((uint8_t)text[i] < ' ' || (uint8_t)text[i] > '~')
			return false;
	return true;
}

static bool board_source_valid(const struct payload_mm_fmp_auth_policy *policy)
{
	if (!text_valid(policy->mainboard_vendor,
		policy->mainboard_vendor_size,
		PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE) ||
	    !text_valid(policy->mainboard_part, policy->mainboard_part_size,
		PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE))
		return false;
#if ENV_TEST
	return true;
#else
	return policy->mainboard_vendor_size ==
		 sizeof(CONFIG_MAINBOARD_VENDOR) - 1 &&
		policy->mainboard_part_size ==
		 sizeof(CONFIG_MAINBOARD_PART_NUMBER) - 1 &&
		!memcmp(policy->mainboard_vendor, CONFIG_MAINBOARD_VENDOR,
			policy->mainboard_vendor_size) &&
		!memcmp(policy->mainboard_part, CONFIG_MAINBOARD_PART_NUMBER,
			policy->mainboard_part_size);
#endif
}

static bool xdr_valid(const uint8_t *data, size_t size)
{
	size_t offset = 0;

	if (!data || !size || size > PAYLOAD_MM_MAX_TRUST_XDR_SIZE)
		return false;
	while (offset < size) {
		size_t certificate_size;
		size_t padding;

		if (size - offset < sizeof(uint32_t))
			return false;
		certificate_size = read_be32(data + offset);
		offset += sizeof(uint32_t);
		if (!certificate_size || certificate_size > size - offset)
			return false;
		offset += certificate_size;
		padding = (4U - (offset & 3U)) & 3U;
		if (padding > size - offset)
			return false;
		while (padding--) {
			if (data[offset++] != 0)
				return false;
		}
	}
	return offset == size;
}

enum cb_err payload_mm_fmp_auth_policy_install(
	const struct payload_mm_fmp_auth_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	struct payload_mm_fmp_auth_policy policy;
	bool protected;

	if (auth_policy.install_attempted)
		return CB_ERR;
	auth_policy.install_attempted = true;
	if (!payload_mm_fmp_owner_ready() || !trusted_policy ||
	    !storage_is_protected)
		return CB_ERR;
	memcpy(&policy, trusted_policy, sizeof(policy));
	if (policy.revision != PAYLOAD_MM_FMP_AUTH_POLICY_REVISION ||
	    policy.size != sizeof(policy) || !policy.image_size ||
	    policy.image_size > PAYLOAD_MM_FMP_MAX_ROM_SIZE ||
	    !bytes_present(policy.image_type.b, sizeof(policy.image_type.b)) ||
	    !xdr_valid(policy.trust_xdr, policy.trust_xdr_size) ||
	    !board_source_valid(&policy) ||
	    payload_mm_authvar_buffers_overlap(trusted_policy, sizeof(policy),
		&auth_policy, sizeof(auth_policy)) ||
	    payload_mm_authvar_buffers_overlap(policy.trust_xdr,
		policy.trust_xdr_size, &auth_policy, sizeof(auth_policy)) ||
	    payload_mm_authvar_buffers_overlap(policy.mainboard_vendor,
		policy.mainboard_vendor_size, &auth_policy, sizeof(auth_policy)) ||
	    payload_mm_authvar_buffers_overlap(policy.mainboard_part,
		policy.mainboard_part_size, &auth_policy, sizeof(auth_policy)) ||
	    payload_mm_fmp_owner_storage_overlaps(policy.trust_xdr,
		policy.trust_xdr_size) ||
	    payload_mm_fmp_owner_storage_overlaps(policy.mainboard_vendor,
		policy.mainboard_vendor_size) ||
	    payload_mm_fmp_owner_storage_overlaps(policy.mainboard_part,
		policy.mainboard_part_size))
		return CB_ERR;
	memcpy(auth_policy.trust_xdr, policy.trust_xdr, policy.trust_xdr_size);
	memcpy(auth_policy.vendor, policy.mainboard_vendor,
		policy.mainboard_vendor_size);
	memcpy(auth_policy.part, policy.mainboard_part,
		policy.mainboard_part_size);
	auth_policy.image_type = policy.image_type;
	auth_policy.trust_xdr_size = policy.trust_xdr_size;
	auth_policy.vendor_size = policy.mainboard_vendor_size;
	auth_policy.part_size = policy.mainboard_part_size;
	auth_policy.trusted_lowest_version = policy.trusted_lowest_version;
	auth_policy.image_size = policy.image_size;
	protected = storage_is_protected(context, &auth_policy,
		sizeof(auth_policy));
	if (!protected || memcmp(trusted_policy, &policy, sizeof(policy)) ||
	    memcmp(auth_policy.trust_xdr, policy.trust_xdr,
		policy.trust_xdr_size) ||
	    memcmp(auth_policy.vendor, policy.mainboard_vendor,
		policy.mainboard_vendor_size) ||
	    memcmp(auth_policy.part, policy.mainboard_part,
		policy.mainboard_part_size) ||
	    memcmp(&auth_policy.image_type, &policy.image_type,
		sizeof(policy.image_type)) ||
	    auth_policy.trust_xdr_size != policy.trust_xdr_size ||
	    auth_policy.vendor_size != policy.mainboard_vendor_size ||
	    auth_policy.part_size != policy.mainboard_part_size ||
	    auth_policy.trusted_lowest_version != policy.trusted_lowest_version ||
	    auth_policy.image_size != policy.image_size || auth_policy.installed ||
	    !auth_policy.install_attempted || auth_policy.busy ||
	    !bytes_zero((const uint8_t *)&auth_policy.crypto,
		sizeof(auth_policy.crypto)) ||
	    !bytes_zero(auth_policy.trust_xdr + policy.trust_xdr_size,
		sizeof(auth_policy.trust_xdr) - policy.trust_xdr_size) ||
	    !bytes_zero((const uint8_t *)auth_policy.vendor +
		policy.mainboard_vendor_size,
		sizeof(auth_policy.vendor) - policy.mainboard_vendor_size) ||
	    !bytes_zero((const uint8_t *)auth_policy.part +
		policy.mainboard_part_size,
		sizeof(auth_policy.part) - policy.mainboard_part_size)) {
		memset(&auth_policy, 0, sizeof(auth_policy));
		auth_policy.install_attempted = true;
		return CB_ERR;
	}
	auth_policy.installed = true;
	return CB_SUCCESS;
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
	uint32_t installed_version, size_t *size)
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
			    memcmp(data + offset, &auth_policy.image_type,
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

static bool padded_name(const uint8_t *name, const char *expected)
{
	size_t length = strlen(expected);

	return length < FMAP_NAME_SIZE && !memcmp(name, expected, length) &&
		name[length] == 0;
}

static bool padded_name_valid(const uint8_t *name)
{
	bool terminated = false;
	bool character = false;

	for (size_t i = 0; i < FMAP_NAME_SIZE; i++) {
		if (!name[i]) {
			terminated = true;
			continue;
		}
		if (terminated || name[i] <= ' ' || name[i] > '~')
			return false;
		character = true;
	}
	return character && terminated;
}

static bool locate_coreboot(const uint8_t *image, size_t image_size,
	struct span *coreboot)
{
	size_t matches = 0;

	if (image_size < FMAP_HEADER_SIZE)
		return false;
	for (size_t offset = 0; offset <= image_size - FMAP_HEADER_SIZE; offset++) {
		const uint8_t *header = image + offset;
		uint16_t count;
		uint32_t coreboot_offset = 0;
		uint32_t coreboot_size = 0;
		uint32_t fmap_offset = 0;
		uint32_t fmap_size = 0;

		if (memcmp(header, fmap_signature, sizeof(fmap_signature) - 1))
			continue;
		if (header[8] != 1 || read_le64(header + 10) ||
		    read_le32(header + 18) != image_size ||
		    !padded_name_valid(header + 22))
			continue;
		count = read_le16(header + 54);
		if (!count || count > FMAP_MAX_AREAS ||
		    (size_t)count * FMAP_AREA_SIZE >
			image_size - offset - FMAP_HEADER_SIZE)
			continue;
		for (size_t i = 0; i < count; i++) {
			const uint8_t *area = header + FMAP_HEADER_SIZE +
				i * FMAP_AREA_SIZE;
			uint32_t area_offset = read_le32(area);
			uint32_t area_size = read_le32(area + 4);

			if (!padded_name_valid(area + 8) || !area_size ||
			    area_offset > image_size ||
			    area_size > image_size - area_offset) {
				coreboot_size = 0;
				break;
			}
			for (size_t j = 0; j < i; j++)
				if (!memcmp(area + 8,
				    header + FMAP_HEADER_SIZE + j * FMAP_AREA_SIZE + 8,
				    FMAP_NAME_SIZE)) {
					coreboot_size = 0;
					goto next_fmap;
				}
			if (padded_name(area + 8, "COREBOOT")) {
				coreboot_offset = area_offset;
				coreboot_size = area_size;
			}
			if (padded_name(area + 8, "FMAP")) {
				fmap_offset = area_offset;
				fmap_size = area_size;
			}
		}
		if (!coreboot_size || !fmap_size || offset < fmap_offset ||
		    offset - fmap_offset > fmap_size || FMAP_HEADER_SIZE +
		    (size_t)count * FMAP_AREA_SIZE >
			fmap_size - (offset - fmap_offset))
			continue;
		*coreboot = (struct span){ image + coreboot_offset, coreboot_size };
		matches++;
next_fmap:
		;
	}
	return matches == 1;
}

static bool cbfs_name(const uint8_t *name, size_t size, const char *expected)
{
	size_t length = strlen(expected);

	return length < size && name[length] == 0 &&
		!memcmp(name, expected, length);
}

static bool zero_bytes(const uint8_t *data, size_t start, size_t end)
{
	while (start < end)
		if (data[start++] != 0)
			return false;
	return true;
}

static bool attributes_valid(const uint8_t *file, size_t start, size_t end)
{
	while (start < end) {
		uint32_t length;

		if (2 * sizeof(uint32_t) > end - start)
			return false;
		length = read_be32(file + start + sizeof(uint32_t));
		if (length < 2 * sizeof(uint32_t) || (length & 3) ||
		    length > end - start)
			return false;
		start += length;
	}
	return start == end;
}

static bool locate_build_info(const struct span *coreboot, struct span *info)
{
	size_t offset = 0;
	size_t files = 0;
	size_t matches = 0;

	while (offset < coreboot->size) {
		const uint8_t *file = coreboot->data + offset;
		uint32_t data_size;
		uint32_t type;
		uint32_t attributes_offset;
		uint32_t data_offset;
		size_t filename_end;
		size_t next;
		bool empty;

		if (coreboot->size - offset < CBFS_HEADER_SIZE ||
		    memcmp(file, cbfs_magic, sizeof(cbfs_magic) - 1)) {
			offset = coreboot->size - offset < CBFS_ALIGNMENT ?
				coreboot->size : offset + CBFS_ALIGNMENT;
			continue;
		}
		if (++files > CBFS_MAX_FILES)
			return false;
		data_size = read_be32(file + 8);
		type = read_be32(file + 12);
		empty = type == CBFS_TYPE_DELETED || type == CBFS_TYPE_NULL;
		attributes_offset = read_be32(file + 16);
		data_offset = read_be32(file + 20);
		if (data_offset <= CBFS_HEADER_SIZE || (data_offset & 3) ||
		    data_offset > 256 || data_offset > coreboot->size - offset ||
		    data_size > coreboot->size - offset - data_offset)
			return false;
		for (filename_end = CBFS_HEADER_SIZE;
		     filename_end < data_offset && file[filename_end]; filename_end++)
			;
		if (filename_end == data_offset ||
		    (filename_end == CBFS_HEADER_SIZE && !empty) ||
		    (filename_end != CBFS_HEADER_SIZE &&
		     !text_valid((const char *)file + CBFS_HEADER_SIZE,
			filename_end - CBFS_HEADER_SIZE,
			256 - CBFS_HEADER_SIZE)) ||
		    (attributes_offset &&
		     ((attributes_offset & 3) || attributes_offset <= filename_end ||
		      attributes_offset >= data_offset ||
		      !zero_bytes(file, filename_end + 1, attributes_offset) ||
		      !attributes_valid(file, attributes_offset, data_offset))) ||
		    (!attributes_offset &&
		     !zero_bytes(file, filename_end + 1, data_offset)))
			return false;
		if (!empty && cbfs_name(file + CBFS_HEADER_SIZE,
			data_offset - CBFS_HEADER_SIZE, "build_info")) {
			if (++matches != 1 || type != CBFS_TYPE_RAW || attributes_offset ||
			    !data_size || data_size > BUILD_INFO_LIMIT)
				return false;
			*info = (struct span){ file + data_offset, data_size };
		}
		next = ALIGN_UP(offset + data_offset + data_size, CBFS_ALIGNMENT);
		if (next <= offset || next > coreboot->size)
			return false;
		offset = next;
	}
	return matches == 1;
}

static bool consume_line(const struct span *info, size_t *offset,
	const char *prefix, struct span *value)
{
	size_t prefix_size = strlen(prefix);
	size_t start;

	if (*offset > info->size || prefix_size > info->size - *offset ||
	    memcmp(info->data + *offset, prefix, prefix_size))
		return false;
	*offset += prefix_size;
	start = *offset;
	while (*offset < info->size && info->data[*offset] != '\n')
		(*offset)++;
	if (*offset == info->size || *offset == start)
		return false;
	*value = (struct span){ info->data + start, *offset - start };
	(*offset)++;
	return true;
}

static bool board_matches(const uint8_t *image, size_t size)
{
	struct span coreboot;
	struct span info;
	struct span version;
	struct span vendor;
	struct span part;
	size_t offset = 0;

	return size == auth_policy.image_size && locate_coreboot(image, size,
		&coreboot) && locate_build_info(&coreboot, &info) &&
		consume_line(&info, &offset, "COREBOOT_VERSION: ", &version) &&
		consume_line(&info, &offset, "MAINBOARD_VENDOR: ", &vendor) &&
		consume_line(&info, &offset, "MAINBOARD_PART_NUMBER: ", &part) &&
		offset == info.size && version.size <= 256 &&
		vendor.size == auth_policy.vendor_size &&
		part.size == auth_policy.part_size &&
		!memcmp(vendor.data, auth_policy.vendor, vendor.size) &&
		!memcmp(part.data, auth_policy.part, part.size);
}

static enum cb_err authenticate(const void *image, size_t image_size,
	uint32_t attempted_version,
	const struct payload_mm_fmp_owner_record *expected_record,
	struct capsule_broker_raw_image *raw_image)
{
	struct payload_mm_fmp_owner_record record;
	struct payload_mm_authenticated_image authenticated;
	struct payload_mm_crypto_span image_span = { image, image_size };
	struct payload_mm_crypto_span trust_span = {
		auth_policy.trust_xdr, auth_policy.trust_xdr_size
	};
	const uint8_t *payload;
	size_t payload_size;
	size_t dependency_size = 0;
	uint32_t header_size;
	uint32_t installed_version;
	uint32_t lowest_version;
	bool valid = false;

	memset(&record, 0, sizeof(record));
	memset(&authenticated, 0, sizeof(authenticated));
	if (payload_mm_fmp_owner_read(PAYLOAD_MM_FMP_STATE_KEY_STATE,
		&record) != CB_SUCCESS ||
	    memcmp(&record, expected_record, sizeof(record)) ||
	    !record.present || !record.data[0])
		goto out;
	installed_version = read_le32(record.data + 4);
	lowest_version = auth_policy.trusted_lowest_version;
	if (record.data[1] && read_le32(record.data + 8) > lowest_version)
		lowest_version = read_le32(record.data + 8);
	if (attempted_version < lowest_version ||
	    payload_mm_authenticate_image(&auth_policy.crypto, &image_span,
		&trust_span, &authenticated) != PAYLOAD_MM_VERIFY_OK)
		goto out;
	payload = authenticated.payload.data;
	payload_size = authenticated.payload.size;
	if (payload_size < MSS1_HEADER_SIZE + 1)
		goto out;
	if (read_le32(payload) != MSS1_SIGNATURE &&
	    !dependency(payload, payload_size, installed_version,
		&dependency_size))
		goto out;
	if (dependency_size > payload_size ||
	    payload_size - dependency_size < MSS1_HEADER_SIZE + 1 ||
	    read_le32(payload + dependency_size) != MSS1_SIGNATURE)
		goto out;
	header_size = read_le32(payload + dependency_size + 4);
	if (header_size < MSS1_HEADER_SIZE ||
	    header_size >= payload_size - dependency_size ||
	    read_le32(payload + dependency_size + 8) != attempted_version ||
	    read_le32(payload + dependency_size + 12) > attempted_version)
		goto out;
	payload += dependency_size + header_size;
	payload_size -= dependency_size + header_size;
	valid = board_matches(payload, payload_size);
	if (valid) {
		uintptr_t image_address = (uintptr_t)image;
		uintptr_t payload_address = (uintptr_t)payload;

		if (payload_address < image_address ||
		    payload_address - image_address > image_size ||
		    payload_size > image_size - (payload_address - image_address))
			valid = false;
		else
			*raw_image = (struct capsule_broker_raw_image) {
				.offset = payload_address - image_address,
				.size = payload_size,
				.lowest_supported_version =
					read_le32(authenticated.payload.data +
						dependency_size + 12),
			};
	}
out:
	memset(&record, 0, sizeof(record));
	memset(&authenticated, 0, sizeof(authenticated));
	return valid ? CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_fmp_authenticate_provider(const void *context,
	const void *capsule, size_t capsule_size, uint32_t attempted_version,
	const struct payload_mm_fmp_owner_record *owner_record,
	struct capsule_broker_raw_image *raw_image)
{
	struct payload_mm_fmp_owner_record owner_snapshot;
	enum cb_err status = CB_ERR;

	if (!raw_image || payload_mm_authvar_buffers_overlap(raw_image,
		sizeof(*raw_image), &auth_policy, sizeof(auth_policy)) ||
	    (capsule && payload_mm_authvar_buffers_overlap(raw_image,
		sizeof(*raw_image), capsule, capsule_size)))
		return CB_ERR;
	memset(raw_image, 0, sizeof(*raw_image));
	if (context || !auth_policy.installed || auth_policy.busy || !capsule ||
	    !capsule_size || !owner_record || !owner_record->sequence ||
	    payload_mm_fmp_owner_storage_overlaps(capsule, capsule_size) ||
	    payload_mm_authvar_buffers_overlap(capsule, capsule_size, &auth_policy,
		sizeof(auth_policy)))
		return CB_ERR;
	owner_snapshot = *owner_record;
	auth_policy.busy = true;
	status = authenticate(capsule, capsule_size, attempted_version,
		&owner_snapshot, raw_image);
	auth_policy.busy = false;
	if (status != CB_SUCCESS)
		memset(raw_image, 0, sizeof(*raw_image));
	return status;
}

#if ENV_TEST
const void *payload_mm_fmp_auth_policy_test_authority(size_t *size)
{
	*size = sizeof(auth_policy);
	return &auth_policy;
}
#endif
