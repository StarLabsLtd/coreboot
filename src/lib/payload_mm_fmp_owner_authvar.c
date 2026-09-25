/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>

#include "payload_mm_authvar_internal.h"
#include "payload_mm_fmp_owner_authvar_internal.h"

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authvar-backed FMP owner must only be built in SMM"
#endif

#define FMP_IDENTITY_COUNT \
	(PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION + 1U)

struct fmp_identity_table {
	struct payload_mm_fmp_state_identity entry[FMP_IDENTITY_COUNT];
};

struct fmp_identity_control {
	uint32_t install_attempted;
	uint32_t installed;
	uint32_t poisoned;
};

static struct {
	struct fmp_identity_table identities;
	struct fmp_identity_table sealed_identities;
	struct fmp_identity_control control;
	struct fmp_identity_control sealed_control;
} authvar_owner;

static bool identities_valid(void)
{
	return !memcmp(&authvar_owner.control, &authvar_owner.sealed_control,
			sizeof(authvar_owner.control)) &&
		authvar_owner.control.install_attempted == 1 &&
		authvar_owner.control.installed == 1 &&
		authvar_owner.control.poisoned == 0 &&
		!memcmp(&authvar_owner.identities, &authvar_owner.sealed_identities,
			sizeof(authvar_owner.identities));
}

static bool guid_present(const guid_t *guid)
{
	uint8_t bits = 0;

	for (size_t i = 0; i < sizeof(guid->b); i++)
		bits |= guid->b[i];
	return bits != 0;
}

static bool identity_valid(uint32_t key,
	const struct payload_mm_fmp_state_identity *identity)
{
	static const char *const names[] = {
		"FmpState", "FmpVersion", "FmpLsv", "LastAttemptStatus",
		"LastAttemptVersion",
	};
	static const char hex[] = "0123456789ABCDEF";
	size_t length = strlen(names[key]);
	size_t suffix = identity->hardware_instance ? 16U : 0U;
	size_t units = length + suffix + 1U;

	if (!guid_present(&identity->namespace_guid) ||
	    identity->variable_name_bytes != units * sizeof(uint16_t) ||
	    identity->variable_name_bytes > sizeof(identity->variable_name))
		return false;
	for (size_t i = 0; i < length; i++)
		if (identity->variable_name[i] != (uint8_t)names[key][i])
			return false;
	for (size_t i = 0; i < suffix; i++) {
		unsigned int shift = (unsigned int)(15U - i) * 4U;

		if (identity->variable_name[length + i] != (uint8_t)
			hex[(identity->hardware_instance >> shift) & 0xfU])
			return false;
	}
	for (size_t i = length + suffix; i < ARRAY_SIZE(identity->variable_name);
	     i++)
		if (identity->variable_name[i])
			return false;
	return true;
}

static bool table_valid(const struct fmp_identity_table *table)
{
	const struct payload_mm_fmp_state_identity *first = &table->entry[0];

	for (uint32_t key = 0; key < FMP_IDENTITY_COUNT; key++) {
		const struct payload_mm_fmp_state_identity *identity =
			&table->entry[key];

		if (!identity_valid(key, identity) ||
		    memcmp(identity->namespace_guid.b, first->namespace_guid.b,
			sizeof(first->namespace_guid.b)) ||
		    identity->hardware_instance != first->hardware_instance ||
		    identity->trusted_lowest_version !=
			first->trusted_lowest_version)
			return false;
	}
	return true;
}

enum cb_err payload_mm_fmp_owner_authvar_identity_install(
	payload_mm_authvar_protected_storage storage_is_protected, void *context)
{
	if (authvar_owner.control.install_attempted ||
	    authvar_owner.sealed_control.install_attempted)
		return CB_ERR;
	authvar_owner.control.install_attempted = 1;
	authvar_owner.sealed_control.install_attempted = 1;
	if (!storage_is_protected ||
	    !storage_is_protected(context, &authvar_owner, sizeof(authvar_owner)))
		goto fail;
	for (uint32_t key = 0; key < FMP_IDENTITY_COUNT; key++)
		if (payload_mm_fmp_state_identity_get_for_key(key,
			&authvar_owner.identities.entry[key]) != CB_SUCCESS)
			goto fail;
	if (!table_valid(&authvar_owner.identities))
		goto fail;
	authvar_owner.sealed_identities = authvar_owner.identities;
	authvar_owner.control.installed = 1;
	authvar_owner.sealed_control.installed = 1;
	return CB_SUCCESS;

fail:
	memset(&authvar_owner.identities, 0, sizeof(authvar_owner.identities));
	memset(&authvar_owner.sealed_identities, 0,
		sizeof(authvar_owner.sealed_identities));
	authvar_owner.control.poisoned = 1;
	authvar_owner.sealed_control.poisoned = 1;
	return CB_ERR;
}

enum cb_err payload_mm_fmp_owner_authvar_identity(uint32_t key,
	struct payload_mm_fmp_state_identity *identity)
{
	if (!identity || key >= FMP_IDENTITY_COUNT || !identities_valid())
		return CB_ERR;
	*identity = authvar_owner.sealed_identities.entry[key];
	return CB_SUCCESS;
}

bool payload_mm_fmp_owner_authvar_storage_overlaps(const void *buffer,
	size_t size)
{
	return payload_mm_authvar_buffers_overlap(buffer, size, &authvar_owner,
		sizeof(authvar_owner));
}

enum payload_mm_fmp_owner_authvar_reservation
payload_mm_fmp_owner_authvar_reservation(const uint8_t vendor_guid[16],
	const void *name, size_t name_size)
{
	if (!vendor_guid || !name || !identities_valid())
		return PAYLOAD_MM_FMP_OWNER_AUTHVAR_AUTHORITY_INVALID;
	for (uint32_t key = 0; key < FMP_IDENTITY_COUNT; key++) {
		const struct payload_mm_fmp_state_identity *identity =
			&authvar_owner.sealed_identities.entry[key];

		if (name_size == identity->variable_name_bytes &&
		    !memcmp(vendor_guid, identity->namespace_guid.b,
			sizeof(identity->namespace_guid.b)) &&
		    !memcmp(name, identity->variable_name, name_size))
			return PAYLOAD_MM_FMP_OWNER_AUTHVAR_RESERVED;
	}
	return PAYLOAD_MM_FMP_OWNER_AUTHVAR_NOT_RESERVED;
}

#if ENV_TEST
const void *payload_mm_fmp_owner_authvar_test_storage(size_t *size)
{
	if (size)
		*size = sizeof(authvar_owner);
	return &authvar_owner;
}

void payload_mm_fmp_owner_authvar_test_corrupt_identity(bool sealed)
{
	struct fmp_identity_table *table = sealed ?
		&authvar_owner.sealed_identities : &authvar_owner.identities;

	table->entry[0].variable_name[0] ^= 1U;
}

void payload_mm_fmp_owner_authvar_test_corrupt_control(bool sealed)
{
	struct fmp_identity_control *control = sealed ?
		&authvar_owner.sealed_control : &authvar_owner.control;

	control->installed ^= 1U;
}
#endif
