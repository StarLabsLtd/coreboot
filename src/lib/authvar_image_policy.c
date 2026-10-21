/* SPDX-License-Identifier: GPL-2.0-only */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <boot/authvar_image_policy.h>

static uint32_t read_u32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8U |
		(uint32_t)bytes[2] << 16U | (uint32_t)bytes[3] << 24U;
}

bool authvar_image_policy_snapshot_decode_shape(const void *data, size_t size,
	struct authvar_image_policy_snapshot_view *output)
{
	const uint8_t *bytes = data;
	struct authvar_image_policy_snapshot_view parsed;
	size_t offset = AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE;
	uint32_t modes;

	if (!data || !output || size < AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE ||
	    size > AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_SIZE ||
	    size > UINTPTR_MAX - (uintptr_t)data ||
	    sizeof(*output) > UINTPTR_MAX - (uintptr_t)output ||
	    (uintptr_t)output % _Alignof(struct authvar_image_policy_snapshot_view) ||
	    !((uintptr_t)output >= (uintptr_t)data + size ||
	      (uintptr_t)data >= (uintptr_t)output + sizeof(*output)))
		return false;
	if (read_u32(bytes) != AUTHVAR_IMAGE_POLICY_SNAPSHOT_REVISION ||
	    read_u32(bytes + 4U) != AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE ||
	    read_u32(bytes + 8U) != size ||
	    read_u32(bytes + 12U) != AUTHVAR_IMAGE_POLICY_SNAPSHOT_KEYS ||
	    read_u32(bytes + 20U) != 6U || read_u32(bytes + 24U) || read_u32(bytes + 28U))
		return false;
	modes = read_u32(bytes + 16U);
	if (modes & ~(uint32_t)(AUTHVAR_IMAGE_POLICY_SNAPSHOT_SETUP |
	    AUTHVAR_IMAGE_POLICY_SNAPSHOT_SECURE_BOOT | AUTHVAR_IMAGE_POLICY_SNAPSHOT_VENDOR_KEYS))
		return false;
	memset(&parsed, 0, sizeof(parsed));
	parsed.modes = modes;
	for (size_t key = 0; key < AUTHVAR_IMAGE_POLICY_SNAPSHOT_KEYS; key++) {
		const uint8_t *descriptor = bytes + 32U + key * 16U;
		const uint32_t present = read_u32(descriptor);
		const uint32_t attributes = read_u32(descriptor + 4U);
		const uint32_t length = read_u32(descriptor + 8U);
		size_t padded;

		if (present > 1U || read_u32(descriptor + 12U) ||
		    (!present && (attributes || length)))
			return false;
		if (!present)
			continue;
		/* Known stored attributes; trust-key admission is a separate profile. */
		if (!length || length > AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_VARIABLE_SIZE ||
		    !attributes || attributes & ~0x2fU ||
		    ((attributes & 4U) && !(attributes & 2U)) ||
		    ((attributes & 8U) && (attributes & 7U) != 7U))
			return false;
		padded = ((size_t)length + 7U) & ~(size_t)7U;
		if (padded > size - offset)
			return false;
		for (size_t index = length; index < padded; index++)
			if (bytes[offset + index])
				return false;
		parsed.blobs[key] = (struct authvar_image_policy_snapshot_blob) {
			.data = bytes + offset, .size = length, .attributes = attributes,
		};
		offset += padded;
	}
	if (offset != size ||
	    !!parsed.blobs[AUTHVAR_IMAGE_POLICY_SNAPSHOT_PK].data ==
		!!(modes & AUTHVAR_IMAGE_POLICY_SNAPSHOT_SETUP) ||
	    ((modes & AUTHVAR_IMAGE_POLICY_SNAPSHOT_SECURE_BOOT) &&
	     !parsed.blobs[AUTHVAR_IMAGE_POLICY_SNAPSHOT_PK].data))
		return false;
	*output = parsed;
	return true;
}
