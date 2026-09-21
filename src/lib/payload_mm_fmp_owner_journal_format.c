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

#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
static bool receipt_shape_valid(
	const struct payload_mm_fmp_owner_auth_receipt *receipt)
{
	return receipt &&
		receipt->magic == PAYLOAD_MM_FMP_OWNER_AUTH_RECEIPT_MAGIC &&
		receipt->revision == PAYLOAD_MM_FMP_OWNER_AUTH_RECEIPT_REVISION &&
		receipt->size == sizeof(*receipt) && receipt->capsule_size &&
		receipt->capsule_size <= UINT32_MAX &&
		receipt->capsule_size <= SIZE_MAX &&
		receipt->digest_algorithm ==
			PAYLOAD_MM_FMP_CAPSULE_DIGEST_SHA256 &&
		receipt->digest_size == PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE &&
		!bytes_equal_value(receipt->capsule_digest,
			sizeof(receipt->capsule_digest), 0);
}

static void encode_le32(uint8_t output[4], uint32_t value)
{
	for (size_t i = 0; i < 4; i++)
		output[i] = (uint8_t)(value >> (i * 8));
}

static void encode_le64(uint8_t output[8], uint64_t value)
{
	for (size_t i = 0; i < 8; i++)
		output[i] = (uint8_t)(value >> (i * 8));
}

bool payload_mm_fmp_owner_authorized_anchor_input(
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	const struct payload_mm_fmp_owner_journal_anchor *current,
	uint64_t generation, uint64_t transaction,
	const struct payload_mm_fmp_owner_auth_receipt *receipt,
	struct payload_mm_fmp_owner_authorized_anchor_input *input)
{
	static const uint8_t domain[
		PAYLOAD_MM_FMP_OWNER_AUTHORIZED_ANCHOR_DOMAIN_SIZE] =
		"PAYLOAD-MM-FMP-AUTH-ANCHOR-V2";
	size_t offset = 0;

	if (!manifest || !current || !input || !generation || !transaction ||
	    !payload_mm_fmp_owner_journal_anchor_valid(current) ||
	    !receipt_shape_valid(receipt))
		return false;
	memcpy(input->bytes + offset, domain, sizeof(domain));
	offset += sizeof(domain);
	memcpy(input->bytes + offset, manifest, sizeof(*manifest));
	offset += sizeof(*manifest);
	encode_le64(input->bytes + offset, current->epoch);
	offset += sizeof(current->epoch);
	memcpy(input->bytes + offset, current->digest, sizeof(current->digest));
	offset += sizeof(current->digest);
	encode_le64(input->bytes + offset, generation);
	offset += sizeof(generation);
	encode_le64(input->bytes + offset, transaction);
	offset += sizeof(transaction);
	encode_le64(input->bytes + offset, receipt->magic);
	offset += sizeof(receipt->magic);
	encode_le32(input->bytes + offset, receipt->revision);
	offset += sizeof(receipt->revision);
	encode_le32(input->bytes + offset, receipt->size);
	offset += sizeof(receipt->size);
	encode_le64(input->bytes + offset, receipt->capsule_size);
	offset += sizeof(uint64_t);
	encode_le32(input->bytes + offset, receipt->digest_algorithm);
	offset += sizeof(uint32_t);
	encode_le32(input->bytes + offset, receipt->digest_size);
	offset += sizeof(uint32_t);
	memcpy(input->bytes + offset, receipt->capsule_digest,
		sizeof(receipt->capsule_digest));
	offset += sizeof(receipt->capsule_digest);
	return offset == sizeof(*input);
}

bool payload_mm_fmp_owner_transition_material_valid(
	const struct payload_mm_fmp_owner_transition_material *material,
	const uint8_t authorization_digest[PAYLOAD_MM_FMP_CAPSULE_DIGEST_SIZE])
{
	const struct payload_mm_fmp_owner_auth_receipt *receipt;

	if (!material || !authorization_digest ||
	    !capsule_tpm_anchor_authorization_shape_valid(
		&material->authorization))
		return false;
	receipt = &material->receipt;
	/* CapsuleImageSize is a UINT32; staging policy applies a tighter bound. */
	return receipt_shape_valid(receipt) &&
		!memcmp(receipt->authorization_digest, authorization_digest,
			sizeof(receipt->authorization_digest));
}
#endif
