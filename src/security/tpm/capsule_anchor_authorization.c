/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_transition.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>

static bool bytes_zero(const void *data, size_t size)
{
	const uint8_t *bytes = data;
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static bool value_valid(const struct capsule_tpm_anchor_value *value)
{
	return value->epoch &&
		!bytes_zero(value->digest, sizeof(value->digest));
}

static bool policy_authorization_valid(
	const struct capsule_tpm_anchor_policy_authorization *policy,
	size_t modulus_size)
{
	return !bytes_zero(policy->approved_policy,
			sizeof(policy->approved_policy)) &&
		!bytes_zero(policy->cp_hash, sizeof(policy->cp_hash)) &&
		!bytes_zero(policy->nonce, sizeof(policy->nonce)) &&
		policy->signature_size == modulus_size &&
		bytes_zero(policy->reserved, sizeof(policy->reserved)) &&
		!bytes_zero(policy->signature, policy->signature_size) &&
		bytes_zero(policy->signature + policy->signature_size,
			sizeof(policy->signature) - policy->signature_size) &&
		!policy->reserved2;
}

bool capsule_tpm_anchor_authorization_shape_valid(
	const struct capsule_tpm_anchor_authorization *authorization)
{
	bool modulus_size_valid;

	if (!authorization)
		return false;
	modulus_size_valid = authorization->modulus_size == 256 ||
		authorization->modulus_size == 384 ||
		authorization->modulus_size == 512;
	return authorization->revision ==
			CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION &&
		authorization->size == sizeof(*authorization) &&
		authorization->policy_revision ==
			CAPSULE_TPM_ANCHOR_POLICY_REVISION &&
		(authorization->nv_index & 0xff000000U) == HR_NV_INDEX &&
		authorization->generation && authorization->transaction &&
		authorization->authority_name[0] == (TPM_ALG_SHA256 >> 8) &&
		authorization->authority_name[1] == (TPM_ALG_SHA256 & 0xff) &&
		!bytes_zero(authorization->authority_name + sizeof(uint16_t),
			CAPSULE_TPM_ANCHOR_POLICY_SIZE) &&
		value_valid(&authorization->current) &&
		value_valid(&authorization->candidate) &&
		authorization->current.epoch != UINT64_MAX &&
		authorization->candidate.epoch ==
			authorization->current.epoch + 1 &&
		memcmp(authorization->current.digest,
			authorization->candidate.digest,
			sizeof(authorization->current.digest)) &&
		authorization->policy_ref_size &&
		authorization->policy_ref_size <=
			CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE &&
		bytes_zero(authorization->policy_ref +
			authorization->policy_ref_size,
			sizeof(authorization->policy_ref) -
			authorization->policy_ref_size) &&
		modulus_size_valid && !authorization->reserved &&
		!bytes_zero(authorization->modulus,
			authorization->modulus_size) &&
		bytes_zero(authorization->modulus + authorization->modulus_size,
			sizeof(authorization->modulus) -
			authorization->modulus_size) &&
		!authorization->reserved2 &&
		policy_authorization_valid(&authorization->write,
			authorization->modulus_size) &&
		policy_authorization_valid(&authorization->lock,
			authorization->modulus_size) &&
		memcmp(authorization->write.approved_policy,
			authorization->lock.approved_policy,
			sizeof(authorization->write.approved_policy)) &&
		memcmp(authorization->write.cp_hash,
			authorization->lock.cp_hash,
			sizeof(authorization->write.cp_hash));
}

bool capsule_tpm_anchor_authorization_matches_binding(
	const struct capsule_tpm_anchor_authorization *authorization,
	const struct capsule_tpm_anchor_binding *binding)
{
	if (!capsule_tpm_anchor_authorization_shape_valid(authorization) ||
	    !binding ||
	    binding->policy_revision != CAPSULE_TPM_ANCHOR_POLICY_REVISION ||
	    (binding->nv_index & 0xff000000U) != HR_NV_INDEX ||
	    binding->authority_name[0] != (TPM_ALG_SHA256 >> 8) ||
	    binding->authority_name[1] != (TPM_ALG_SHA256 & 0xff) ||
	    bytes_zero(binding->authority_name + sizeof(uint16_t),
		CAPSULE_TPM_ANCHOR_POLICY_SIZE) ||
	    !binding->policy_ref_size ||
	    binding->policy_ref_size > CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE ||
	    !bytes_zero(binding->policy_ref + binding->policy_ref_size,
		sizeof(binding->policy_ref) - binding->policy_ref_size) ||
	    binding->write_locked > 1 ||
	    !bytes_zero(binding->reserved, sizeof(binding->reserved)))
		return false;
	return authorization->policy_revision == binding->policy_revision &&
		authorization->nv_index == binding->nv_index &&
		!memcmp(authorization->authority_name, binding->authority_name,
			sizeof(authorization->authority_name)) &&
		authorization->policy_ref_size == binding->policy_ref_size &&
		!memcmp(authorization->policy_ref, binding->policy_ref,
			sizeof(authorization->policy_ref));
}
