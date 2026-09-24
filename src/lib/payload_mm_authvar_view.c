/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_bundle.h>
#include <boot/payload_mm_authvar_view.h>
#include <commonlib/helpers.h>
#include <string.h>

static const uint8_t global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t cert_db_guid[16] = {
	0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
	0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
};
static const uint8_t setup_mode_name[] = {
	'S', 0, 'e', 0, 't', 0, 'u', 0, 'p', 0, 'M', 0, 'o', 0, 'd', 0,
	'e', 0, 0, 0,
};
static const uint8_t signature_support_name[] = {
	'S', 0, 'i', 0, 'g', 0, 'n', 0, 'a', 0, 't', 0, 'u', 0, 'r', 0,
	'e', 0, 'S', 0, 'u', 0, 'p', 0, 'p', 0, 'o', 0, 'r', 0, 't', 0,
	0, 0,
};
static const uint8_t secure_boot_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 0, 0,
};
static const uint8_t cert_db_volatile_name[] = {
	'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 'v', 0, 0, 0,
};
static const uint8_t vendor_keys_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 0, 0,
};
static const uint8_t disabled;
static const uint8_t enabled = 1U;
static const uint8_t cert_db_empty[] = { 4, 0, 0, 0 };
static const uint8_t signature_support[] = {
	0x12, 0xa5, 0x6c, 0x82, 0x10, 0xcf, 0xc9, 0x4a,
	0xb1, 0x87, 0xbe, 0x01, 0x49, 0x66, 0x31, 0xbd,
	0x26, 0x16, 0xc4, 0xc1, 0x4c, 0x50, 0x92, 0x40,
	0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28,
	0x07, 0x53, 0x3e, 0xff, 0xd0, 0x9f, 0xc9, 0x48,
	0x85, 0xf1, 0x8a, 0xd5, 0x6c, 0x70, 0x1e, 0x01,
	0xae, 0x0f, 0x3e, 0x09, 0xc4, 0xa6, 0x50, 0x4f,
	0x9f, 0x1b, 0xd4, 0x1e, 0x2b, 0x89, 0xc1, 0x9a,
	0xe8, 0x66, 0x57, 0x3c, 0x9c, 0x26, 0x34, 0x4e,
	0xaa, 0x14, 0xed, 0x77, 0x6e, 0x85, 0xb3, 0xb6,
	0xa1, 0x59, 0xc0, 0xa5, 0xe4, 0x94, 0xa7, 0x4a,
	0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72,
};

struct synthetic_variable {
	const uint8_t *vendor_guid;
	const uint8_t *name;
	uint32_t name_size;
	const uint8_t *data;
	uint32_t data_size;
	uint32_t attributes;
	uint8_t mode;
};

static const struct synthetic_variable synthetic[] = {
	{ global_guid, setup_mode_name, sizeof(setup_mode_name), NULL, 1U,
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		PAYLOAD_MM_AUTHVAR_MODE_SETUP },
	{ global_guid, signature_support_name, sizeof(signature_support_name),
		signature_support, sizeof(signature_support),
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS, 0U },
	{ global_guid, secure_boot_name, sizeof(secure_boot_name), NULL, 1U,
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT },
	{ cert_db_guid, cert_db_volatile_name, sizeof(cert_db_volatile_name),
		cert_db_empty, sizeof(cert_db_empty),
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED, 0U },
	{ global_guid, vendor_keys_name, sizeof(vendor_keys_name), NULL, 1U,
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS },
};

static bool span_valid(const void *data, size_t size)
{
	return size == 0U || (data && (uintptr_t)data <= UINTPTR_MAX - size);
}

static bool spans_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!span_valid(left, left_size) || !span_valid(right, right_size))
		return true;
	if (!left_size || !right_size)
		return false;
	if (left_address <= right_address)
		return right_address - left_address < left_size;
	return left_address - right_address < right_size;
}

static bool name_valid(const void *name, size_t size)
{
	const uint8_t *bytes = name;

	if (!span_valid(name, size) || size < 2U || size > UINT32_MAX ||
	    (size & 1U) ||
	    bytes[size - 2U] || bytes[size - 1U])
		return false;
	for (size_t offset = 0U; offset + 2U < size; offset += 2U)
		if (!bytes[offset] && !bytes[offset + 1U])
			return false;
	return true;
}

static int synthetic_key(const uint8_t vendor_guid[16], const void *name,
	size_t name_size)
{
	for (size_t i = 0U; i < ARRAY_SIZE(synthetic); i++)
		if (name_size == synthetic[i].name_size &&
		    !memcmp(vendor_guid, synthetic[i].vendor_guid, 16U) &&
		    !memcmp(name, synthetic[i].name, name_size))
			return (int)i;
	return -1;
}

static bool view_valid(const struct payload_mm_authvar_view *view)
{
	size_t entry_capacity;

	if (!span_valid(view, sizeof(*view)) ||
	    (uintptr_t)view % _Alignof(*view))
		return false;
	if (!span_valid(view->persistent, sizeof(*view->persistent)) ||
	    (uintptr_t)view->persistent % _Alignof(*view->persistent))
		return false;
	entry_capacity = view->persistent->entry_capacity;
	if (entry_capacity > SIZE_MAX / sizeof(view->persistent->entries[0]) ||
	    view->at_runtime > 1U ||
	    view->volatile_modes & ~(PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
		PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS) ||
	    memcmp(view->reserved, (uint8_t[sizeof(view->reserved)]) { 0 },
		sizeof(view->reserved)) ||
	    !payload_mm_authvar_store_index_valid(view->persistent))
		return false;
	for (size_t i = 0U; i < ARRAY_SIZE(synthetic); i++)
		if (payload_mm_authvar_store_find(view->persistent,
			synthetic[i].vendor_guid, synthetic[i].name,
			synthetic[i].name_size))
			return false;
	return true;
}

static void synthetic_value(const struct payload_mm_authvar_view *view,
	size_t key, struct payload_mm_authvar_view_value *value)
{
	const struct synthetic_variable *variable = &synthetic[key];

	*value = (struct payload_mm_authvar_view_value) {
		.vendor_guid = variable->vendor_guid,
		.name = variable->name,
		.data = variable->data ? variable->data :
			(view->volatile_modes & variable->mode ? &enabled : &disabled),
		.name_size = variable->name_size,
		.data_size = variable->data_size,
		.attributes = variable->attributes,
	};
}

bool payload_mm_authvar_view_key_reserved(const uint8_t vendor_guid[16],
	const void *name, size_t name_size)
{
	return span_valid(vendor_guid, 16U) && name_valid(name, name_size) &&
		synthetic_key(vendor_guid, name, name_size) >= 0;
}

enum cb_err payload_mm_authvar_view_init(struct payload_mm_authvar_view *view,
	const struct payload_mm_authvar_store_index *persistent,
	uint8_t volatile_modes, bool at_runtime)
{
	struct payload_mm_authvar_view draft = {
		.persistent = persistent,
		.volatile_modes = volatile_modes,
		.at_runtime = at_runtime,
	};
	size_t entries_size;
	size_t entry_capacity;

	if (!span_valid(view, sizeof(*view)) || !persistent ||
	    !span_valid(persistent, sizeof(*persistent)) ||
	    (uintptr_t)view % _Alignof(*view) ||
	    (uintptr_t)persistent % _Alignof(*persistent))
		return CB_ERR_ARG;
	entry_capacity = persistent->entry_capacity;
	if (entry_capacity > SIZE_MAX / sizeof(persistent->entries[0]))
		return CB_ERR_ARG;
	entries_size = entry_capacity * sizeof(persistent->entries[0]);
	if (spans_overlap(view, sizeof(*view), persistent, sizeof(*persistent)) ||
	    spans_overlap(view, sizeof(*view), persistent->store,
		persistent->store_size) ||
	    spans_overlap(view, sizeof(*view), persistent->entries, entries_size) ||
	    !view_valid(&draft))
		return CB_ERR_ARG;
	memcpy(view, &draft, sizeof(*view));
	return CB_SUCCESS;
}

uint64_t payload_mm_authvar_view_get(const struct payload_mm_authvar_view *view,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t data_capacity, struct payload_mm_authvar_view_value *value)
{
	struct payload_mm_authvar_view_value draft = { 0 };
	struct payload_mm_authvar_get_result persistent = { 0 };
	const struct payload_mm_authvar_store_entry *entry;
	uint64_t status;
	int key;

	if (!span_valid(value, sizeof(*value)) ||
	    (uintptr_t)value % _Alignof(*value) ||
	    !span_valid(vendor_guid, 16U) ||
	    !name_valid(name, name_size) || !view_valid(view) ||
	    spans_overlap(value, sizeof(*value), view, sizeof(*view)) ||
	    spans_overlap(value, sizeof(*value), vendor_guid, 16U) ||
	    spans_overlap(value, sizeof(*value), name, name_size) ||
	    spans_overlap(value, sizeof(*value), view->persistent,
		sizeof(*view->persistent)) ||
	    spans_overlap(value, sizeof(*value), view->persistent->store,
		view->persistent->store_size) ||
	    spans_overlap(value, sizeof(*value), view->persistent->entries,
		view->persistent->entry_capacity * sizeof(view->persistent->entries[0])))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	key = synthetic_key(vendor_guid, name, name_size);
	if (key >= 0) {
		synthetic_value(view, (size_t)key, &draft);
		status = data_capacity < draft.data_size ?
			PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL :
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			draft.vendor_guid = NULL;
			draft.name = NULL;
			draft.data = NULL;
			draft.name_size = 0U;
		}
	} else {
		status = payload_mm_authvar_store_get(view->persistent, vendor_guid,
			name, name_size, data_capacity, view->at_runtime, &persistent);
		draft.data_size = persistent.required_data_size;
		draft.attributes = persistent.attributes;
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			entry = persistent.entry;
			draft.vendor_guid = entry->vendor_guid;
			draft.name = payload_mm_authvar_store_name(view->persistent, entry);
			draft.data = payload_mm_authvar_store_data(view->persistent, entry);
			draft.name_size = entry->name_size;
			if (!draft.name || !draft.data)
				return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		}
	}
	memcpy(value, &draft, sizeof(*value));
	return status;
}

uint64_t payload_mm_authvar_view_get_next(
	const struct payload_mm_authvar_view *view,
	const uint8_t vendor_guid[16], const void *name, size_t name_size,
	uint32_t name_capacity, struct payload_mm_authvar_view_value *value)
{
	static const uint8_t zero_guid[16];
	struct payload_mm_authvar_view_value draft = { 0 };
	struct payload_mm_authvar_next_result persistent = { 0 };
	const struct payload_mm_authvar_store_entry *entry;
	uint64_t status;
	int key = -1;

	if (!span_valid(value, sizeof(*value)) ||
	    (uintptr_t)value % _Alignof(*value) ||
	    !span_valid(vendor_guid, 16U) ||
	    !view_valid(view) || name_capacity < sizeof(uint16_t) ||
	    name_size > name_capacity ||
	    (name_size && !name_valid(name, name_size)) ||
	    (!name_size && memcmp(vendor_guid, zero_guid, sizeof(zero_guid))) ||
	    spans_overlap(value, sizeof(*value), view, sizeof(*view)) ||
	    spans_overlap(value, sizeof(*value), vendor_guid, 16U) ||
	    spans_overlap(value, sizeof(*value), name, name_size) ||
	    spans_overlap(value, sizeof(*value), view->persistent,
		sizeof(*view->persistent)) ||
	    spans_overlap(value, sizeof(*value), view->persistent->store,
		view->persistent->store_size) ||
	    spans_overlap(value, sizeof(*value), view->persistent->entries,
		view->persistent->entry_capacity * sizeof(view->persistent->entries[0])))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (name_size)
		key = synthetic_key(vendor_guid, name, name_size);
	if (!name_size || (key >= 0 && (size_t)key + 1U < ARRAY_SIZE(synthetic))) {
		size_t next = name_size ? (size_t)key + 1U : 0U;

		synthetic_value(view, next, &draft);
		status = name_capacity < draft.name_size ?
			PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL :
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			draft.vendor_guid = NULL;
			draft.name = NULL;
			draft.data = NULL;
			draft.data_size = 0U;
			draft.attributes = 0U;
		}
	} else {
		const void *cursor_name = name;
		const uint8_t *cursor_guid = vendor_guid;
		size_t cursor_size = name_size;

		if (key >= 0) {
			cursor_name = NULL;
			cursor_guid = zero_guid;
			cursor_size = 0U;
		}
		status = payload_mm_authvar_store_get_next(view->persistent,
			cursor_guid, cursor_name, cursor_size, name_capacity,
			view->at_runtime, &persistent);
		draft.name_size = persistent.required_name_size;
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			entry = persistent.entry;
			draft.vendor_guid = entry->vendor_guid;
			draft.name = payload_mm_authvar_store_name(view->persistent, entry);
			draft.data = payload_mm_authvar_store_data(view->persistent, entry);
			draft.name_size = entry->name_size;
			draft.data_size = entry->data_size;
			draft.attributes = entry->attributes;
			if (!draft.name || !draft.data)
				return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
		}
	}
	memcpy(value, &draft, sizeof(*value));
	return status;
}

uint64_t payload_mm_authvar_view_query(const struct payload_mm_authvar_view *view,
	const struct payload_mm_authvar_store_policy *policy, uint32_t attributes,
	struct payload_mm_authvar_query_result *result)
{
	if (!span_valid(result, sizeof(*result)) ||
	    (uintptr_t)result % _Alignof(*result) || !span_valid(policy,
		sizeof(*policy)) || !view_valid(view) ||
	    (uintptr_t)policy % _Alignof(*policy) ||
	    spans_overlap(result, sizeof(*result), view, sizeof(*view)) ||
	    spans_overlap(result, sizeof(*result), policy, sizeof(*policy)) ||
	    spans_overlap(result, sizeof(*result), view->persistent,
		sizeof(*view->persistent)) ||
	    spans_overlap(result, sizeof(*result), view->persistent->store,
		view->persistent->store_size) ||
	    spans_overlap(result, sizeof(*result), view->persistent->entries,
		view->persistent->entry_capacity * sizeof(view->persistent->entries[0])))
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	return payload_mm_authvar_store_query(view->persistent, policy, attributes,
		view->at_runtime, result);
}
