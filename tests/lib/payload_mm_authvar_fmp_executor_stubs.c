/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>

#include "../../src/lib/payload_mm_authvar_internal.h"
#include "../../src/lib/payload_mm_fmp_owner_authvar_internal.h"

extern const void *payload_mm_fmp_test_owner_storage;
extern size_t payload_mm_fmp_test_owner_storage_size;
static uint8_t authvar_owner_storage[256] __aligned(8);
extern uint64_t payload_mm_fmp_test_hardware_instance;

enum cb_err payload_mm_fmp_owner_authvar_identity(uint32_t key,
	struct payload_mm_fmp_state_identity *identity)
{
	static const char *const names[] = {
		"FmpState", "FmpVersion", "FmpLsv", "LastAttemptStatus",
		"LastAttemptVersion",
	};
	static const char hex[] = "0123456789ABCDEF";
	size_t length;

	if (key >= ARRAY_SIZE(names) || !identity)
		return CB_ERR;
	memset(identity, 0, sizeof(*identity));
	identity->namespace_guid.b[0] = 1;
	identity->hardware_instance = payload_mm_fmp_test_hardware_instance;
	length = strlen(names[key]);
	for (size_t i = 0; i < length; i++)
		identity->variable_name[i] = (uint8_t)names[key][i];
	if (identity->hardware_instance)
		for (size_t i = 0; i < 16U; i++) {
			unsigned int shift = (unsigned int)(15U - i) * 4U;

			identity->variable_name[length + i] = (uint8_t)
				hex[(identity->hardware_instance >> shift) & 0xfU];
		}
	length += identity->hardware_instance ? 16U : 0U;
	identity->variable_name_bytes = (length + 1U) * sizeof(uint16_t);
	return CB_SUCCESS;
}

bool payload_mm_fmp_owner_authvar_storage_overlaps(const void *buffer,
	size_t size)
{
	return payload_mm_authvar_buffers_overlap(buffer, size,
		authvar_owner_storage, sizeof(authvar_owner_storage)) ||
		(payload_mm_fmp_test_owner_storage &&
		 buffer == payload_mm_fmp_test_owner_storage &&
		 size == payload_mm_fmp_test_owner_storage_size);
}

const void *payload_mm_fmp_owner_authvar_test_storage(size_t *size)
{
	if (size)
		*size = sizeof(authvar_owner_storage);
	return authvar_owner_storage;
}

enum payload_mm_fmp_owner_authvar_reservation
payload_mm_fmp_owner_authvar_reservation(const uint8_t vendor_guid[16],
	const void *name, size_t name_size)
{
	(void)vendor_guid;
	(void)name;
	(void)name_size;
	return PAYLOAD_MM_FMP_OWNER_AUTHVAR_NOT_RESERVED;
}

#ifndef EXECUTOR_REAL_MEDIA
bool payload_mm_authvar_authority_ready(void)
{
	return true;
}
#endif
