/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "payload_mm_fmp_owner_journal_internal.h"

static bool bytes_equal_value(const uint8_t *data, size_t size, uint8_t value)
{
	uint8_t difference = 0;

	for (size_t i = 0; i < size; i++)
		difference |= data[i] ^ value;
	return difference == 0;
}

bool payload_mm_fmp_owner_journal_anchor_valid(
	const struct payload_mm_fmp_owner_journal_anchor *anchor)
{
	return anchor && anchor->epoch && anchor->epoch != UINT64_MAX &&
		!bytes_equal_value(anchor->digest, sizeof(anchor->digest), 0);
}

bool payload_mm_fmp_owner_journal_anchor_equal(
	const struct payload_mm_fmp_owner_journal_anchor *left,
	const struct payload_mm_fmp_owner_journal_anchor *right)
{
	return left && right && left->epoch == right->epoch &&
		!memcmp(left->digest, right->digest, sizeof(left->digest));
}

bool payload_mm_fmp_owner_journal_manifest_shape_valid(
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	u32 slot_size)
{
	return manifest &&
		manifest->magic == PAYLOAD_MM_FMP_OWNER_JOURNAL_MAGIC &&
		manifest->revision == PAYLOAD_MM_FMP_OWNER_JOURNAL_REVISION &&
		manifest->size == sizeof(*manifest) &&
		manifest->slot_size == slot_size &&
		manifest->owner_record_size ==
			sizeof(struct payload_mm_fmp_owner_record) &&
		manifest->owner_record_count == PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS &&
		manifest->format == PAYLOAD_MM_FMP_OWNER_JOURNAL_FORMAT &&
		manifest->epoch;
}

static bool state_data_valid(const uint8_t data[PAYLOAD_MM_FMP_STATE_WIRE_SIZE])
{
	for (size_t i = 0; i < 4; i++)
		if (data[i] > 1)
			return false;
	return true;
}

static bool owner_record_valid(uint32_t key,
	const struct payload_mm_fmp_owner_record *record)
{
	uint32_t expected_size = key == PAYLOAD_MM_FMP_STATE_KEY_STATE ?
		PAYLOAD_MM_FMP_STATE_WIRE_SIZE : sizeof(uint32_t);

	if (key > PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION ||
	    !record->sequence || record->present > 1 || record->reserved ||
	    record->reserved2)
		return false;
	if (!record->present)
		return !record->attributes && !record->data_size &&
			bytes_equal_value(record->data, sizeof(record->data), 0);
	if (record->attributes != PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES ||
	    record->data_size != expected_size ||
	    (key != PAYLOAD_MM_FMP_STATE_KEY_STATE &&
	     !bytes_equal_value(record->data + sizeof(uint32_t),
		sizeof(record->data) - sizeof(uint32_t), 0)))
		return false;
	return key != PAYLOAD_MM_FMP_STATE_KEY_STATE ||
		state_data_valid(record->data);
}

bool payload_mm_fmp_owner_journal_manifest_records_valid(
	const struct payload_mm_fmp_owner_journal_manifest *manifest)
{
	if (!manifest)
		return false;
	for (uint32_t key = 0; key < PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS; key++)
		if (!owner_record_valid(key, &manifest->record[key]))
			return false;
	return true;
}

bool payload_mm_fmp_owner_journal_prepared_shape_valid(
	const struct payload_mm_fmp_owner_prepared *prepared)
{
	return prepared &&
		prepared->magic == PAYLOAD_MM_FMP_OWNER_PREPARED_MAGIC &&
		prepared->revision == PAYLOAD_MM_FMP_OWNER_PREPARED_REVISION &&
		prepared->size == sizeof(*prepared) &&
		(prepared->state == PAYLOAD_MM_FMP_OWNER_PREPARED_STATE ||
		 prepared->state == PAYLOAD_MM_FMP_OWNER_COMMITTED_STATE) &&
		!prepared->reserved && prepared->generation &&
		prepared->transaction &&
		payload_mm_fmp_owner_journal_anchor_valid(&prepared->current) &&
		prepared->candidate.epoch &&
		!bytes_equal_value(prepared->candidate.digest,
			sizeof(prepared->candidate.digest), 0) &&
		prepared->candidate.epoch == prepared->current.epoch + 1 &&
		memcmp(prepared->current.digest, prepared->candidate.digest,
			sizeof(prepared->current.digest));
}
