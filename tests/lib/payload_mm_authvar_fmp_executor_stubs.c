/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>

#include "../../src/lib/payload_mm_authvar_internal.h"
#include "../../src/lib/payload_mm_fmp_owner_authvar_internal.h"

extern const void *payload_mm_fmp_test_owner_storage;
extern size_t payload_mm_fmp_test_owner_storage_size;
static uint8_t authvar_owner_storage[256] __aligned(8);

enum cb_err payload_mm_fmp_owner_authvar_identity(uint32_t key,
	struct payload_mm_fmp_state_identity *identity)
{
	static const uint16_t name[] = {
		'F', 'm', 'p', 'S', 't', 'a', 't', 'e', 0,
	};

	if (key != PAYLOAD_MM_FMP_STATE_KEY_STATE || !identity)
		return CB_ERR;
	memset(identity, 0, sizeof(*identity));
	identity->namespace_guid.b[0] = 1;
	identity->variable_name_bytes = sizeof(name);
	memcpy(identity->variable_name, name, sizeof(name));
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

bool payload_mm_authvar_authority_ready(void)
{
	return true;
}
