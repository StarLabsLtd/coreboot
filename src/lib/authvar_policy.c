/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <boot/authvar_policy.h>
#include <boot/payload_mm_authvar_service.h>
#include <boot/payload_mm_authvar_store.h>
#include <string.h>

struct lock_state {
	uint8_t name_space[16];
	uint8_t value, padding;
} __packed;
_Static_assert(sizeof(struct lock_state) == 18, "VariablePolicy lock-state ABI");

static bool status_error(uint64_t status)
{
	return (status & PAYLOAD_MM_AUTHVAR_STATUS_ERROR_BIT) != 0;
}

static size_t name_size(const uint16_t *name, size_t maximum)
{
	for (size_t i = 0; i < maximum / sizeof(*name); i++)
		if (!name[i])
			return i ? (i + 1U) * sizeof(*name) : 0;
	return 0;
}

static const struct authvar_policy_entry *best_policy(
	const struct authvar_policy *store, const uint16_t *name, size_t bytes,
	const void *guid, bool literal)
{
	const struct authvar_policy_entry *best = NULL;
	size_t best_priority = UINT8_MAX;
	for (size_t i = 0; i < store->count; i++) {
		const struct authvar_policy_entry *entry = (const void *)store->entries[i];
		const uint16_t *pattern = (const void *)(store->entries[i] + entry->offset_to_name);
		size_t priority = UINT8_MAX;
		if (memcmp(guid, entry->name_space, sizeof(entry->name_space)))
			continue;
		if (entry->offset_to_name != entry->size) {
			if ((size_t)(entry->size - entry->offset_to_name) != bytes)
				continue;
			bool matches = true;
			priority = 0;
			for (size_t j = 0; j + 1U < bytes / sizeof(*name); j++) {
				bool wildcard = !literal && pattern[j] == '#';
				bool hex = (name[j] >= '0' && name[j] <= '9') ||
					(name[j] >= 'A' && name[j] <= 'F') ||
					(name[j] >= 'a' && name[j] <= 'f');
				matches &= wildcard ? hex : pattern[j] == name[j];
				priority += wildcard;
			}
			if (!matches)
				continue;
		}
		if (!best || priority < best_priority) {
			best = entry;
			best_priority = priority;
		}
	}
	return best;
}

bool authvar_policy_has_match(const struct authvar_policy *policy,
	const uint16_t *name, size_t name_size, const void *guid)
{
	return best_policy(policy, name, name_size, guid, false) != NULL;
}

uint64_t authvar_policy_register(struct authvar_policy *store,
	const struct authvar_policy_entry *entry)
{
	size_t minimum = sizeof(*entry);
	if (store->locked)
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	if (entry->version != AUTHVAR_POLICY_REVISION || entry->size < minimum ||
	    entry->size > AUTHVAR_POLICY_BYTES || (entry->size & 1U) ||
	    (entry->offset_to_name & 1U) || entry->offset_to_name < minimum ||
	    entry->offset_to_name > entry->size || !entry->max_size ||
	    entry->min_size > entry->max_size || entry->lock_policy_type > 3U)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	if (entry->lock_policy_type == 3U) {
		minimum += sizeof(struct lock_state);
		if (entry->offset_to_name <= minimum ||
		    name_size((const void *)((const uint8_t *)entry + minimum),
			entry->offset_to_name - minimum) != entry->offset_to_name - minimum)
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	} else if (entry->offset_to_name != minimum) {
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	const uint16_t *name = (const void *)((const uint8_t *)entry + entry->offset_to_name);
	size_t bytes = entry->size - entry->offset_to_name;
	if (bytes) {
		if (name_size(name, bytes) != bytes)
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
		size_t wildcards = 0;
		for (size_t i = 0; i + 1U < bytes / sizeof(*name); i++)
			wildcards += name[i] == '#';
		if (wildcards > UINT8_MAX)
			return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	const struct authvar_policy_entry *existing =
		best_policy(store, name, bytes, entry->name_space, true);
	if (existing && (size_t)(existing->size - existing->offset_to_name) == bytes)
		return PAYLOAD_MM_AUTHVAR_STATUS_ALREADY_STARTED;
	if (store->count == AUTHVAR_POLICY_SLOTS)
		return PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES;
	memcpy(store->entries[store->count++], entry, entry->size);
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

uint64_t authvar_policy_check(const struct authvar_policy *store,
	const uint16_t *name, const void *guid, uint32_t attributes, size_t size,
	authvar_policy_get_fn *get, void *context,
	const struct authvar_policy_entry **rejected)
{
	if (rejected)
		*rejected = NULL;
	size_t bytes = name_size(name, PAYLOAD_MM_AUTHVAR_STORE_DEFAULT_MAX_NAME_SIZE);
	if (!bytes)
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	const struct authvar_policy_entry *entry = best_policy(store, name, bytes, guid, false);
	if (!entry)
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	bool locked = entry->lock_policy_type == 1U;
	if (entry->lock_policy_type == 2U) {
		size_t existing_size = 0;
		uint64_t status = get(context, name, guid, NULL, &existing_size, NULL);
		if (status_error(status) && status != PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND &&
		    status != PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
			return status;
		locked = status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
			status == PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL;
	} else if (entry->lock_policy_type == 3U) {
		const struct lock_state *state = (const void *)((const uint8_t *)entry + sizeof(*entry));
		uint8_t value;
		size_t value_size = sizeof(value);
		uint64_t status = get(context, (const void *)(state + 1), state->name_space,
			NULL, &value_size, &value);
		if (status_error(status) && status != PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND &&
		    status != PAYLOAD_MM_AUTHVAR_STATUS_BUFFER_TOO_SMALL)
			return status;
		locked = status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			value_size == sizeof(value) && value == state->value;
	}
	if (locked) {
		if (rejected)
			*rejected = entry;
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	}
	if ((size || attributes & PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE) &&
	    (size < entry->min_size || size > entry->max_size ||
	     (attributes & entry->attributes_must_have) != entry->attributes_must_have ||
	     attributes & entry->attributes_cant_have)) {
		if (rejected)
			*rejected = entry;
		return PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}
