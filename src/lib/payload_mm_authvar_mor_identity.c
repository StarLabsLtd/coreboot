/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_identity.h>
#include <stdbool.h>
#include <string.h>

static bool range_valid(const void *pointer, size_t size)
{
	return !size || (pointer &&
		(uintptr_t)pointer <= UINTPTR_MAX - (size - 1U));
}

static const uint16_t control_name[] = {
	'M', 'e', 'm', 'o', 'r', 'y', 'O', 'v', 'e', 'r', 'w', 'r', 'i', 't',
	'e', 'R', 'e', 'q', 'u', 'e', 's', 't', 'C', 'o', 'n', 't', 'r', 'o',
	'l', 0U,
};
static const uint16_t lock_name[] = {
	'M', 'e', 'm', 'o', 'r', 'y', 'O', 'v', 'e', 'r', 'w', 'r', 'i', 't',
	'e', 'R', 'e', 'q', 'u', 'e', 's', 't', 'C', 'o', 'n', 't', 'r', 'o',
	'l', 'L', 'o', 'c', 'k', 0U,
};

const struct payload_mm_authvar_mor_identity payload_mm_authvar_mor_control_identity = {
	.vendor_guid = {
		0xbe, 0x39, 0x09, 0xe2, 0xd4, 0x32, 0xbe, 0x41,
		0xa1, 0x50, 0x89, 0x7f, 0x85, 0xd4, 0x98, 0x29,
	},
	.name = control_name,
	.name_size = sizeof(control_name),
};

const struct payload_mm_authvar_mor_identity payload_mm_authvar_mor_lock_identity = {
	.vendor_guid = {
		0xcf, 0x3c, 0x98, 0xbb, 0x1d, 0x15, 0xe1, 0x40,
		0xa0, 0x7b, 0x4a, 0x17, 0xbe, 0x16, 0x82, 0x92,
	},
	.name = lock_name,
	.name_size = sizeof(lock_name),
};

_Static_assert(sizeof(control_name) == PAYLOAD_MM_AUTHVAR_MOR_CONTROL_NAME_SIZE,
	"MOR Control name size changed");
_Static_assert(sizeof(lock_name) == PAYLOAD_MM_AUTHVAR_MOR_LOCK_NAME_SIZE,
	"MOR Lock name size changed");

enum payload_mm_authvar_mor_variable payload_mm_authvar_mor_classify(
	const uint8_t vendor_guid[PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE],
	const void *name, size_t name_size)
{
	if (!range_valid(vendor_guid, PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE) ||
	    !range_valid(name, name_size))
		return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE;
	if (name_size == payload_mm_authvar_mor_control_identity.name_size &&
	    !memcmp(vendor_guid, payload_mm_authvar_mor_control_identity.vendor_guid,
		PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE) &&
	    !memcmp(name, payload_mm_authvar_mor_control_identity.name, name_size))
		return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL;
	if (name_size == payload_mm_authvar_mor_lock_identity.name_size &&
	    !memcmp(vendor_guid, payload_mm_authvar_mor_lock_identity.vendor_guid,
		PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE) &&
	    !memcmp(name, payload_mm_authvar_mor_lock_identity.name, name_size))
		return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK;
	return PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE;
}
