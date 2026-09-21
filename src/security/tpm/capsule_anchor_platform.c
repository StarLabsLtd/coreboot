/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_platform.h>
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
	return value->epoch && !bytes_zero(value->digest,
		sizeof(value->digest));
}

bool capsule_tpm_anchor_platform_binding_valid(
	const struct capsule_tpm_anchor_binding *binding)
{
	return binding && binding->policy_revision ==
			CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION &&
		(binding->nv_index & 0xff000000U) == HR_NV_INDEX &&
		bytes_zero(binding->authority_name,
			sizeof(binding->authority_name)) &&
		!binding->policy_ref_size &&
		bytes_zero(binding->policy_ref, sizeof(binding->policy_ref)) &&
		binding->write_locked <= 1 &&
		bytes_zero(binding->reserved, sizeof(binding->reserved));
}

bool capsule_tpm_anchor_platform_request_valid(
	const struct capsule_tpm_anchor_platform_request *request,
	const struct capsule_tpm_anchor_binding *binding)
{
	if (!request || !capsule_tpm_anchor_platform_binding_valid(binding))
		return false;
	return request->revision ==
			CAPSULE_TPM_ANCHOR_PLATFORM_REQUEST_REVISION &&
		request->size == sizeof(*request) &&
		request->policy_revision ==
			CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION &&
		request->policy_revision == binding->policy_revision &&
		request->nv_index == binding->nv_index && request->generation &&
		request->transaction && value_valid(&request->current) &&
		value_valid(&request->candidate) &&
		request->current.epoch != UINT64_MAX &&
		request->candidate.epoch == request->current.epoch + 1 &&
		memcmp(request->current.digest, request->candidate.digest,
			sizeof(request->current.digest));
}

enum cb_err capsule_tpm_anchor_platform_grant_validate(
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
	    snapshot.policy_revision !=
		CAPSULE_TPM_ANCHOR_PLATFORM_POLICY_REVISION ||
	    snapshot.policy_revision != binding_snapshot.policy_revision ||
	    snapshot.nv_index != binding_snapshot.nv_index ||
	    !snapshot.generation || !snapshot.transaction ||
	    snapshot.flags != CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS ||
	    snapshot.reserved ||
	    !capsule_tpm_anchor_platform_request_valid(
		&(struct capsule_tpm_anchor_platform_request) {
			.revision = CAPSULE_TPM_ANCHOR_PLATFORM_REQUEST_REVISION,
			.size = sizeof(struct capsule_tpm_anchor_platform_request),
			.policy_revision = snapshot.policy_revision,
			.nv_index = snapshot.nv_index,
			.generation = snapshot.generation,
			.transaction = snapshot.transaction,
			.current = snapshot.current,
			.candidate = snapshot.candidate,
		}, &binding_snapshot) ||
	    binding_snapshot.write_locked != 1)
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
} platform_authority;

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_base = (uintptr_t)left;
	uintptr_t right_base = (uintptr_t)right;

	if (!left_size || !right_size || left_base > UINTPTR_MAX - left_size ||
	    right_base > UINTPTR_MAX - right_size)
		return true;
	return left_base < right_base + right_size &&
		right_base < left_base + left_size;
}

enum cb_err capsule_tpm_anchor_platform_grant_install(
	const struct capsule_tpm_anchor_grant *trusted_grant,
	const struct capsule_tpm_anchor_binding *trusted_binding,
	capsule_tpm_anchor_grant_protected_storage storage_is_protected,
	void *context)
{
	struct capsule_tpm_anchor_grant grant;
	struct capsule_tpm_anchor_binding binding;
	bool protected;

	if (platform_authority.install_attempted)
		return CB_ERR;
	platform_authority.install_attempted = true;
	if (!trusted_grant || !trusted_binding || !storage_is_protected ||
	    ranges_overlap(trusted_grant, sizeof(*trusted_grant),
		&platform_authority, sizeof(platform_authority)) ||
	    ranges_overlap(trusted_binding, sizeof(*trusted_binding),
		&platform_authority, sizeof(platform_authority)) ||
	    ranges_overlap(trusted_grant, sizeof(*trusted_grant), trusted_binding,
		sizeof(*trusted_binding)))
		return CB_ERR;
	memcpy(&grant, trusted_grant, sizeof(grant));
	memcpy(&binding, trusted_binding, sizeof(binding));
	if (capsule_tpm_anchor_platform_grant_validate(&grant, &binding) !=
		CB_SUCCESS)
		return CB_ERR;
	protected = storage_is_protected(context, &platform_authority,
		sizeof(platform_authority));
	if (!protected || !platform_authority.install_attempted ||
	    platform_authority.installed || platform_authority.consumed ||
	    platform_authority.poisoned ||
	    !bytes_zero(&platform_authority.grant,
		sizeof(platform_authority.grant)) ||
	    memcmp(&grant, trusted_grant, sizeof(grant)) ||
	    memcmp(&binding, trusted_binding, sizeof(binding))) {
		memset(&platform_authority, 0, sizeof(platform_authority));
		platform_authority.install_attempted = true;
		platform_authority.poisoned = true;
		return CB_ERR;
	}
	platform_authority.grant = grant;
	platform_authority.installed = true;
	return CB_SUCCESS;
}

bool capsule_tpm_anchor_platform_grant_ready(void)
{
	return platform_authority.installed && !platform_authority.consumed &&
		!platform_authority.poisoned;
}

static enum cb_err consume_finish(bool valid)
{
	memset(&platform_authority.grant, 0, sizeof(platform_authority.grant));
	if (!valid)
		platform_authority.poisoned = true;
	return valid ? CB_SUCCESS : CB_ERR;
}

enum cb_err capsule_tpm_anchor_platform_grant_consume(uint64_t generation,
	uint64_t transaction, const struct capsule_tpm_anchor_value *current,
	const struct capsule_tpm_anchor_value *candidate)
{
	struct capsule_tpm_anchor_value current_snapshot;
	struct capsule_tpm_anchor_value candidate_snapshot;
	bool valid;

	if (!capsule_tpm_anchor_platform_grant_ready())
		return CB_ERR;
	platform_authority.consumed = true;
	if (!current || !candidate ||
	    ranges_overlap(current, sizeof(*current), &platform_authority,
		sizeof(platform_authority)) ||
	    ranges_overlap(candidate, sizeof(*candidate), &platform_authority,
		sizeof(platform_authority)) ||
	    ranges_overlap(current, sizeof(*current), candidate,
		sizeof(*candidate)))
		return consume_finish(false);
	memcpy(&current_snapshot, current, sizeof(current_snapshot));
	memcpy(&candidate_snapshot, candidate, sizeof(candidate_snapshot));
	valid = generation == platform_authority.grant.generation &&
		transaction == platform_authority.grant.transaction &&
		!memcmp(&current_snapshot, &platform_authority.grant.current,
			sizeof(current_snapshot)) &&
		!memcmp(&candidate_snapshot, &platform_authority.grant.candidate,
			sizeof(candidate_snapshot)) &&
		!memcmp(current, &current_snapshot, sizeof(current_snapshot)) &&
		!memcmp(candidate, &candidate_snapshot, sizeof(candidate_snapshot));
	return consume_finish(valid);
}

#endif
