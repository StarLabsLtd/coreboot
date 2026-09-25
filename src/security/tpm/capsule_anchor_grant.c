/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_grant.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	for (size_t i = 0; i < size; i++)
		value |= bytes[i];
	return value == 0;
}

static bool value_valid(const struct capsule_tpm_anchor_value *value)
{
	return value->epoch && !bytes_zero(value->digest, sizeof(value->digest));
}

static bool binding_valid(const struct capsule_tpm_anchor_binding *binding)
{
	return binding->policy_revision == CAPSULE_TPM_ANCHOR_POLICY_REVISION &&
		(binding->nv_index & 0xff000000U) == HR_NV_INDEX &&
		binding->authority_name[0] == (TPM_ALG_SHA256 >> 8) &&
		binding->authority_name[1] == (TPM_ALG_SHA256 & 0xff) &&
		!bytes_zero(binding->authority_name + sizeof(uint16_t),
			CAPSULE_TPM_ANCHOR_POLICY_SIZE) &&
		binding->policy_ref_size &&
		binding->policy_ref_size <= CAPSULE_TPM_ANCHOR_POLICY_REF_MAX_SIZE &&
		bytes_zero(binding->policy_ref + binding->policy_ref_size,
			sizeof(binding->policy_ref) - binding->policy_ref_size) &&
		binding->write_locked == 1 &&
		bytes_zero(binding->reserved, sizeof(binding->reserved));
}

enum cb_err capsule_tpm_anchor_grant_validate(
	const struct capsule_tpm_anchor_grant *grant,
	const struct capsule_tpm_anchor_binding *binding)
{
	struct capsule_tpm_anchor_grant snapshot;
	struct capsule_tpm_anchor_binding binding_snapshot;

	if (!grant || !binding)
		return CB_ERR_ARG;
	memcpy(&snapshot, grant, sizeof(snapshot));
	memcpy(&binding_snapshot, binding, sizeof(binding_snapshot));
	if (snapshot.revision != CAPSULE_TPM_ANCHOR_GRANT_REVISION ||
	    snapshot.size != sizeof(snapshot) ||
	    snapshot.policy_revision != binding_snapshot.policy_revision ||
	    snapshot.nv_index != binding_snapshot.nv_index ||
	    !snapshot.generation || !snapshot.transaction ||
	    snapshot.flags != CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS ||
	    snapshot.reserved ||
	    !value_valid(&snapshot.current) ||
	    !value_valid(&snapshot.candidate) ||
	    snapshot.current.epoch == UINT64_MAX ||
	    snapshot.candidate.epoch != snapshot.current.epoch + 1 ||
	    !memcmp(snapshot.current.digest, snapshot.candidate.digest,
		sizeof(snapshot.current.digest)) ||
	    !binding_valid(&binding_snapshot))
		return CB_ERR;
	return CB_SUCCESS;
}

#if ENV_SMM || ENV_TEST

static struct {
	struct capsule_tpm_anchor_grant grant;
	bool installed;
	bool install_attempted;
	bool consumed;
	bool poisoned;
} authority;

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_base = (uintptr_t)left;
	uintptr_t right_base = (uintptr_t)right;
	uintptr_t left_last;
	uintptr_t right_last;

	if (!left || !right || !left_size || !right_size ||
	    left_base > UINTPTR_MAX - (left_size - 1U) ||
	    right_base > UINTPTR_MAX - (right_size - 1U))
		return true;
	left_last = left_base + left_size - 1U;
	right_last = right_base + right_size - 1U;
	return left_base <= right_last && right_base <= left_last;
}

enum cb_err capsule_tpm_anchor_grant_install(
	const struct capsule_tpm_anchor_grant *trusted_grant,
	const struct capsule_tpm_anchor_binding *trusted_binding,
	capsule_tpm_anchor_grant_protected_storage storage_is_protected,
	void *context)
{
	struct capsule_tpm_anchor_grant grant;
	struct capsule_tpm_anchor_binding binding;
	bool protected;

	if (authority.install_attempted)
		return CB_ERR;
	authority.install_attempted = true;
	if (!trusted_grant || !trusted_binding || !storage_is_protected ||
	    ranges_overlap(trusted_grant, sizeof(*trusted_grant), &authority,
		sizeof(authority)) ||
	    ranges_overlap(trusted_binding, sizeof(*trusted_binding), &authority,
		sizeof(authority)) ||
	    ranges_overlap(trusted_grant, sizeof(*trusted_grant), trusted_binding,
		sizeof(*trusted_binding)))
		return CB_ERR;
	memcpy(&grant, trusted_grant, sizeof(grant));
	memcpy(&binding, trusted_binding, sizeof(binding));
	if (capsule_tpm_anchor_grant_validate(&grant, &binding) != CB_SUCCESS)
		return CB_ERR;
	protected = storage_is_protected(context, &authority, sizeof(authority));
	if (!protected || !authority.install_attempted || authority.installed ||
	    authority.consumed || authority.poisoned ||
	    !bytes_zero(&authority.grant, sizeof(authority.grant)) ||
	    memcmp(&grant, trusted_grant, sizeof(grant)) ||
	    memcmp(&binding, trusted_binding, sizeof(binding))) {
		memset(&authority, 0, sizeof(authority));
		authority.install_attempted = true;
		authority.poisoned = true;
		return CB_ERR;
	}
	authority.grant = grant;
	authority.installed = true;
	return CB_SUCCESS;
}

bool capsule_tpm_anchor_grant_ready(void)
{
	return authority.installed && !authority.consumed && !authority.poisoned;
}

static enum cb_err consume_finish(bool valid)
{
	memset(&authority.grant, 0, sizeof(authority.grant));
	if (!valid)
		authority.poisoned = true;
	return valid ? CB_SUCCESS : CB_ERR;
}

enum cb_err capsule_tpm_anchor_grant_consume(uint64_t generation,
	uint64_t transaction, const struct capsule_tpm_anchor_value *current,
	const struct capsule_tpm_anchor_value *candidate)
{
	struct capsule_tpm_anchor_value current_snapshot;
	struct capsule_tpm_anchor_value candidate_snapshot;
	bool valid;

	if (!capsule_tpm_anchor_grant_ready())
		return CB_ERR;
	authority.consumed = true;
	if (!current || !candidate ||
	    ranges_overlap(current, sizeof(*current), &authority,
		sizeof(authority)) ||
	    ranges_overlap(candidate, sizeof(*candidate), &authority,
		sizeof(authority)) ||
	    ranges_overlap(current, sizeof(*current), candidate,
		sizeof(*candidate))) {
		return consume_finish(false);
	}
	memcpy(&current_snapshot, current, sizeof(current_snapshot));
	memcpy(&candidate_snapshot, candidate, sizeof(candidate_snapshot));
	valid = generation == authority.grant.generation &&
		transaction == authority.grant.transaction &&
		!memcmp(&current_snapshot, &authority.grant.current,
			sizeof(current_snapshot)) &&
		!memcmp(&candidate_snapshot, &authority.grant.candidate,
			sizeof(candidate_snapshot)) &&
		!memcmp(current, &current_snapshot, sizeof(current_snapshot)) &&
		!memcmp(candidate, &candidate_snapshot, sizeof(candidate_snapshot));
	return consume_finish(valid);
}

#endif
