/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_IDENTITY_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_IDENTITY_H

#include <boot/payload_mm_authvar_service.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE 16U
#define PAYLOAD_MM_AUTHVAR_MOR_CONTROL_NAME_SIZE 60U
#define PAYLOAD_MM_AUTHVAR_MOR_LOCK_NAME_SIZE 68U
#define PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES \
	(PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE | \
	 PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS | \
	 PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS)

enum payload_mm_authvar_mor_variable {
	PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_NONE,
	PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_CONTROL,
	PAYLOAD_MM_AUTHVAR_MOR_VARIABLE_LOCK,
};

struct payload_mm_authvar_mor_identity {
	uint8_t vendor_guid[PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE];
	const uint16_t *name;
	size_t name_size;
};

extern const struct payload_mm_authvar_mor_identity
	payload_mm_authvar_mor_control_identity;
extern const struct payload_mm_authvar_mor_identity
	payload_mm_authvar_mor_lock_identity;

enum payload_mm_authvar_mor_variable payload_mm_authvar_mor_classify(
	const uint8_t vendor_guid[PAYLOAD_MM_AUTHVAR_MOR_GUID_SIZE],
	const void *name, size_t name_size);

#endif
