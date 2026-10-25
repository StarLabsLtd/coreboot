/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_controlled_mode.h>

#include "payload_mm_authvar_internal.h"

#define MODE_ATTRIBUTES \
	(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE | \
	 PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS)

enum payload_mm_authvar_controlled_mode
payload_mm_authvar_controlled_mode_classify(const uint8_t vendor_guid[16],
	const void *name, size_t name_size)
{
	if (payload_mm_authvar_mode_key_matches(vendor_guid, name, name_size,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE))
		return PAYLOAD_MM_AUTHVAR_CONTROLLED_SECURE_BOOT_ENABLE;
	if (payload_mm_authvar_mode_key_matches(vendor_guid, name, name_size,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE))
		return PAYLOAD_MM_AUTHVAR_CONTROLLED_CUSTOM_MODE;
	return PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE;
}

uint64_t payload_mm_authvar_controlled_mode_property(
	enum payload_mm_authvar_controlled_mode mode, uint32_t attributes,
	size_t payload_size)
{
	if (mode == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	if (mode != PAYLOAD_MM_AUTHVAR_CONTROLLED_SECURE_BOOT_ENABLE &&
	    mode != PAYLOAD_MM_AUTHVAR_CONTROLLED_CUSTOM_MODE)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	/* EDK2 VarCheck exempts delete requests from property shape checks. */
	if ((!payload_size && !(attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE)) ||
	    !attributes)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	if (attributes != MODE_ATTRIBUTES ||
	    payload_size != 1U)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t payload_mm_authvar_controlled_mode_authorize(
	enum payload_mm_authvar_controlled_mode mode,
	bool trusted_physical_presence)
{
	if (mode == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	if (mode != PAYLOAD_MM_AUTHVAR_CONTROLLED_SECURE_BOOT_ENABLE &&
	    mode != PAYLOAD_MM_AUTHVAR_CONTROLLED_CUSTOM_MODE)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	return trusted_physical_presence ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION;
}
