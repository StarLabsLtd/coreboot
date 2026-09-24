/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_controlled_mode.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "payload_mm_authvar_internal.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static const uint8_t enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const uint8_t custom_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const uint16_t enable_name[] = {
	'S', 'e', 'c', 'u', 'r', 'e', 'B', 'o', 'o', 't', 'E', 'n', 'a', 'b',
	'l', 'e', 0U,
};
static const uint16_t custom_name[] = {
	'C', 'u', 's', 't', 'o', 'm', 'M', 'o', 'd', 'e', 0U,
};

int main(void)
{
	uint8_t near_guid[16];
	uint16_t near_name[ARRAY_SIZE(enable_name)];
	const uint32_t nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	enum payload_mm_authvar_controlled_mode modes[] = {
		PAYLOAD_MM_AUTHVAR_CONTROLLED_SECURE_BOOT_ENABLE,
		PAYLOAD_MM_AUTHVAR_CONTROLLED_CUSTOM_MODE,
	};

	assert(payload_mm_authvar_controlled_mode_classify(enable_guid, enable_name,
		sizeof(enable_name)) ==
		PAYLOAD_MM_AUTHVAR_CONTROLLED_SECURE_BOOT_ENABLE);
	assert(payload_mm_authvar_controlled_mode_classify(custom_guid, custom_name,
		sizeof(custom_name)) == PAYLOAD_MM_AUTHVAR_CONTROLLED_CUSTOM_MODE);
	memcpy(near_guid, enable_guid, sizeof(near_guid));
	near_guid[15] ^= 1U;
	assert(payload_mm_authvar_controlled_mode_classify(near_guid, enable_name,
		sizeof(enable_name)) == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	memcpy(near_name, enable_name, sizeof(near_name));
	near_name[ARRAY_SIZE(near_name) - 2U] ^= 1U;
	assert(payload_mm_authvar_controlled_mode_classify(enable_guid, near_name,
		sizeof(near_name)) == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	assert(payload_mm_authvar_controlled_mode_classify(enable_guid, enable_name,
		sizeof(enable_name) - 2U) == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	assert(payload_mm_authvar_controlled_mode_classify(NULL, enable_name,
		sizeof(enable_name)) == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	assert(payload_mm_authvar_controlled_mode_classify(enable_guid, NULL,
		sizeof(enable_name)) == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	assert(payload_mm_authvar_controlled_mode_classify(enable_guid, enable_name,
		0U) == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	assert(payload_mm_authvar_controlled_mode_classify(enable_guid,
		(const void *)(uintptr_t)(UINTPTR_MAX - 1U), 4U) ==
		PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	assert(payload_mm_authvar_controlled_mode_classify(
		(const void *)(uintptr_t)(UINTPTR_MAX - 7U), enable_name,
		sizeof(enable_name)) == PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE);
	assert(!payload_mm_authvar_mode_key_matches(
		(const void *)(uintptr_t)(UINTPTR_MAX - 7U), enable_name,
		sizeof(enable_name), PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE));
	assert(!payload_mm_authvar_mode_key_matches(enable_guid,
		(const void *)(uintptr_t)(UINTPTR_MAX - 1U), 4U,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE));

	for (size_t i = 0U; i < ARRAY_SIZE(modes); i++) {
		assert(payload_mm_authvar_controlled_mode_property(modes[i], nv_bs, 1U) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(payload_mm_authvar_controlled_mode_property(modes[i], nv_bs, 2U) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(payload_mm_authvar_controlled_mode_property(modes[i],
			nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE, 1U) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(payload_mm_authvar_controlled_mode_property(modes[i],
			nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE, 0U) ==
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(payload_mm_authvar_controlled_mode_property(modes[i], 0U, 0U) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(payload_mm_authvar_controlled_mode_property(modes[i], nv_bs, 0U) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(payload_mm_authvar_controlled_mode_authorize(modes[i], false) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
		assert(payload_mm_authvar_controlled_mode_authorize(modes[i], true) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	}
	assert(payload_mm_authvar_controlled_mode_property(
		PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE, 0xffffffffU, SIZE_MAX) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(payload_mm_authvar_controlled_mode_authorize(
		PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE, false) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(payload_mm_authvar_controlled_mode_property(
		(enum payload_mm_authvar_controlled_mode)99, nv_bs, 1U) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	return 0;
}
