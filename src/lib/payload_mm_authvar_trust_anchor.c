/* SPDX-License-Identifier: GPL-2.0-only */

#include <payload_mm_cms.h>

#include "payload_mm_crypto/crypto.h"

#include <commonlib/helpers.h>

static bool range_valid(const void *data, size_t size)
{
	return size == 0U ||
		(data != NULL && (uintptr_t)data <= (uintptr_t)-1 - size);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_address = (uintptr_t)left;
	uintptr_t right_address = (uintptr_t)right;

	if (!range_valid(left, left_size) || !range_valid(right, right_size))
		return true;
	if (left_size == 0U || right_size == 0U)
		return false;
	return left_address < right_address + right_size &&
		right_address < left_address + left_size;
}

static bool range_contains(const struct payload_mm_crypto_span *outer,
	const struct payload_mm_crypto_span *inner)
{
	uintptr_t outer_address = (uintptr_t)outer->data;
	uintptr_t inner_address = (uintptr_t)inner->data;

	return range_valid(outer->data, outer->size) &&
		range_valid(inner->data, inner->size) && inner->size != 0U &&
		inner_address >= outer_address &&
		inner_address - outer_address <= outer->size &&
		inner->size <= outer->size - (inner_address - outer_address);
}

static bool inputs_valid(const struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const struct payload_mm_crypto_span *trust_anchor, size_t *signer_index)
{
	const void *descriptors[] = { owner, signed_data, verified, trust_anchor };
	const size_t descriptor_sizes[] = {
		sizeof(*owner), sizeof(*signed_data), sizeof(*verified),
		sizeof(*trust_anchor),
	};
	const void *data_ranges[2];
	size_t data_sizes[2];
	size_t signer_matches = 0U;
	size_t left;
	size_t right;

	if (owner == NULL || signed_data == NULL || verified == NULL ||
	    trust_anchor == NULL || signer_index == NULL ||
	    !range_valid(owner, sizeof(*owner)) ||
	    !range_valid(signed_data, sizeof(*signed_data)) ||
	    !range_valid(verified, sizeof(*verified)) ||
	    !range_valid(trust_anchor, sizeof(*trust_anchor)) ||
	    (uintptr_t)owner % _Alignof(*owner) ||
	    (uintptr_t)signed_data % _Alignof(*signed_data) ||
	    (uintptr_t)verified % _Alignof(*verified) ||
	    (uintptr_t)trust_anchor % _Alignof(*trust_anchor) ||
	    !range_valid(signed_data->data, signed_data->size) ||
	    signed_data->size == 0U || signed_data->size > PAYLOAD_MM_MAX_CMS_SIZE ||
	    !range_valid(trust_anchor->data, trust_anchor->size) ||
	    trust_anchor->size == 0U ||
	    trust_anchor->size > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE ||
	    verified->certificate_count == 0U ||
	    verified->certificate_count > PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES)
		return false;

	data_ranges[0] = signed_data->data;
	data_sizes[0] = signed_data->size;
	data_ranges[1] = trust_anchor->data;
	data_sizes[1] = trust_anchor->size;
	for (left = 0U; left < ARRAY_SIZE(descriptors); left++) {
		for (right = left + 1U; right < ARRAY_SIZE(descriptors); right++)
			if (ranges_overlap(descriptors[left], descriptor_sizes[left],
				descriptors[right], descriptor_sizes[right]))
				return false;
		for (right = 0U; right < ARRAY_SIZE(data_ranges); right++)
			if (ranges_overlap(descriptors[left], descriptor_sizes[left],
				data_ranges[right], data_sizes[right]))
				return false;
	}
	if (ranges_overlap(data_ranges[0], data_sizes[0], data_ranges[1],
		data_sizes[1]))
		return false;

	for (left = 0U; left < verified->certificate_count; left++) {
		if (!range_contains(signed_data, &verified->certificates[left]) ||
		    verified->certificates[left].size >
			PAYLOAD_MM_CRYPTO_MAX_CERTIFICATE_SIZE)
			return false;
		if (verified->signer_certificate.data ==
			verified->certificates[left].data &&
		    verified->signer_certificate.size ==
			verified->certificates[left].size) {
			*signer_index = left;
			signer_matches++;
		}
		for (right = left + 1U; right < verified->certificate_count; right++)
			if (ranges_overlap(verified->certificates[left].data,
				verified->certificates[left].size,
				verified->certificates[right].data,
				verified->certificates[right].size))
				return false;
	}
	return signer_matches == 1U;
}

enum payload_mm_verify_status payload_mm_authvar_trust_anchor_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_cms_verified_signer *verified,
	const struct payload_mm_crypto_span *trust_anchor)
{
	struct payload_mm_crypto_span intermediates[
		PAYLOAD_MM_CRYPTO_MAX_CERTIFICATES - 1U];
	enum payload_mm_crypto_failure_source failure_source;
	enum payload_mm_verify_status status;
	size_t intermediate_count = 0U;
	size_t signer_index = 0U;
	size_t index;

	if (!inputs_valid(owner, signed_data, verified, trust_anchor,
		&signer_index))
		return PAYLOAD_MM_VERIFY_INVALID;

	status = payload_mm_crypto_begin(owner);
	if (status != PAYLOAD_MM_VERIFY_OK)
		return status;
	for (index = 0U; index < verified->certificate_count; index++) {
		if (index != signer_index)
			intermediates[intermediate_count++] = verified->certificates[index];
	}
	status = payload_mm_authvar_x509_chain_verify_detailed(
		&verified->certificates[signer_index], intermediates,
		intermediate_count, trust_anchor, &failure_source);
	return payload_mm_crypto_end(owner, status);
}
