/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_transition.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>

#if ENV_SMM && !ENV_TEST
#error "The capsule TPM transition coordinator must not be built in SMM"
#endif

#define TRANSITION_RUNNING 0x43545231U
#define TRANSITION_FINISHED 0x43544631U

_Static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
	"capsule TPM transition control must be lock-free");

struct transport_context {
	struct tpm_pre_os_lifecycle *lifecycle;
	const struct tpm_pre_os_token *token;
};

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

static bool authorization_valid(
	const struct capsule_tpm_anchor_authorization *authorization,
	const struct capsule_tpm_anchor_binding *binding)
{
	bool modulus_size_valid = authorization->modulus_size == 256 ||
		authorization->modulus_size == 384 ||
		authorization->modulus_size == 512;

	return authorization->revision ==
			CAPSULE_TPM_ANCHOR_AUTHORIZATION_REVISION &&
		authorization->size == sizeof(*authorization) &&
		authorization->policy_revision == binding->policy_revision &&
		authorization->nv_index == binding->nv_index &&
		authorization->generation && authorization->transaction &&
		value_valid(&authorization->current) &&
		value_valid(&authorization->candidate) &&
		authorization->current.epoch != UINT64_MAX &&
		authorization->candidate.epoch ==
			authorization->current.epoch + 1 &&
		memcmp(authorization->current.digest,
			authorization->candidate.digest,
			sizeof(authorization->current.digest)) &&
		!bytes_zero(authorization->approved_policy,
			sizeof(authorization->approved_policy)) &&
		!bytes_zero(authorization->cp_hash,
			sizeof(authorization->cp_hash)) &&
		!memcmp(authorization->authority_name, binding->authority_name,
			sizeof(authorization->authority_name)) &&
		authorization->policy_ref_size == binding->policy_ref_size &&
		!memcmp(authorization->policy_ref, binding->policy_ref,
			sizeof(authorization->policy_ref)) && modulus_size_valid &&
		authorization->signature_size == authorization->modulus_size &&
		!authorization->reserved &&
		!bytes_zero(authorization->nonce, sizeof(authorization->nonce)) &&
		!bytes_zero(authorization->modulus,
			authorization->modulus_size) &&
		bytes_zero(authorization->modulus + authorization->modulus_size,
			sizeof(authorization->modulus) - authorization->modulus_size) &&
		!bytes_zero(authorization->signature,
			authorization->signature_size) &&
		bytes_zero(authorization->signature +
			authorization->signature_size,
			sizeof(authorization->signature) -
			authorization->signature_size) && !authorization->reserved2;
}

static bool provider_valid(
	const struct capsule_tpm_anchor_transition_provider *provider)
{
	return provider->revision ==
			CAPSULE_TPM_ANCHOR_TRANSITION_PROVIDER_REVISION &&
		provider->size == sizeof(*provider) && provider->prepared &&
		provider->read && provider->advance && provider->install &&
		(!!provider->context == !!provider->context_size);
}

static enum cb_err transmit(void *opaque, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct transport_context *context = opaque;

	return tpm_pre_os_lifecycle_transmit(context->lifecycle,
		context->token, request, request_size, response, response_size);
}

static bool inputs_unchanged(
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_binding *binding_snapshot,
	const struct capsule_tpm_anchor_authorization *authorization,
	const struct capsule_tpm_anchor_authorization *authorization_snapshot,
	const struct capsule_tpm_anchor_transition_provider *provider,
	const struct capsule_tpm_anchor_transition_provider *provider_snapshot)
{
	return !memcmp(binding, binding_snapshot, sizeof(*binding)) &&
		!memcmp(authorization, authorization_snapshot,
			sizeof(*authorization)) &&
		!memcmp(provider, provider_snapshot, sizeof(*provider));
}

static bool claim(struct capsule_tpm_anchor_transition *transition)
{
	uint32_t expected = 0;

	return transition && __atomic_compare_exchange_n(&transition->control,
		&expected, TRANSITION_RUNNING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE);
}

static struct capsule_tpm_anchor_grant build_grant(
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_authorization *authorization,
	uint32_t flags)
{
	return (struct capsule_tpm_anchor_grant) {
		.revision = CAPSULE_TPM_ANCHOR_GRANT_REVISION,
		.size = sizeof(struct capsule_tpm_anchor_grant),
		.policy_revision = binding->policy_revision,
		.nv_index = binding->nv_index,
		.generation = authorization->generation,
		.transaction = authorization->transaction,
		.flags = flags,
		.current = authorization->current,
		.candidate = authorization->candidate,
	};
}

enum cb_err capsule_tpm_anchor_transition_run(
	struct capsule_tpm_anchor_transition *transition,
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_authorization *authorization,
	const struct capsule_tpm_anchor_transition_provider *provider)
{
	struct capsule_tpm_anchor_binding binding_snapshot;
	struct capsule_tpm_anchor_authorization authorization_snapshot;
	struct capsule_tpm_anchor_transition_provider provider_snapshot;
	struct capsule_tpm_anchor_grant grant = { 0 };
	struct capsule_tpm_anchor_grant prepared_argument = { 0 };
	struct capsule_tpm_anchor_grant install_argument = { 0 };
	struct capsule_tpm_anchor_value value = { 0 };
	struct tpm_pre_os_token token = { 0 };
	struct transport_context transport_context = {
		.lifecycle = lifecycle,
		.token = &token,
	};
	enum cb_err result = CB_ERR;
	bool acquired = false;
	bool handed_off = false;

	if (!claim(transition))
		return CB_ERR;
	if (!lifecycle || !binding || !authorization || !provider)
		goto out;
	memcpy(&binding_snapshot, binding, sizeof(binding_snapshot));
	memcpy(&authorization_snapshot, authorization,
		sizeof(authorization_snapshot));
	memcpy(&provider_snapshot, provider, sizeof(provider_snapshot));
	if (!binding_valid(&binding_snapshot) ||
	    !authorization_valid(&authorization_snapshot, &binding_snapshot) ||
	    !provider_valid(&provider_snapshot))
		goto out;
	grant = build_grant(&binding_snapshot, &authorization_snapshot, 0);
	prepared_argument = grant;
	if (provider_snapshot.prepared(provider_snapshot.context,
		&prepared_argument) != CB_SUCCESS ||
	    memcmp(&prepared_argument, &grant, sizeof(grant)) ||
	    !inputs_unchanged(binding, &binding_snapshot,
		authorization, &authorization_snapshot, provider,
		&provider_snapshot))
		goto out;
	memset(&prepared_argument, 0, sizeof(prepared_argument));
	grant.flags |= CAPSULE_TPM_ANCHOR_GRANT_MEDIA_PREPARED;
	if (tpm_pre_os_lifecycle_acquire(lifecycle, &token) != CB_SUCCESS)
		goto handoff;
	acquired = true;
	if (provider_snapshot.read(provider_snapshot.context, transmit,
		&transport_context, &binding_snapshot, &value) != CB_SUCCESS ||
	    !inputs_unchanged(binding, &binding_snapshot, authorization,
		&authorization_snapshot, provider, &provider_snapshot))
		goto release;
	if (!memcmp(&value, &authorization_snapshot.current, sizeof(value))) {
		/* The write result is advisory; exact readback resolves ambiguity. */
		(void)provider_snapshot.advance(provider_snapshot.context, transmit,
			&transport_context, &binding_snapshot,
			&authorization_snapshot);
		if (!inputs_unchanged(binding, &binding_snapshot, authorization,
			&authorization_snapshot, provider, &provider_snapshot))
			goto release;
		memset(&value, 0, sizeof(value));
		if (provider_snapshot.read(provider_snapshot.context, transmit,
			&transport_context, &binding_snapshot, &value) !=
			CB_SUCCESS)
			goto release;
	}
	if (memcmp(&value, &authorization_snapshot.candidate, sizeof(value)) ||
	    !inputs_unchanged(binding, &binding_snapshot, authorization,
		&authorization_snapshot, provider, &provider_snapshot))
		goto release;
	grant.flags |= CAPSULE_TPM_ANCHOR_GRANT_TPM_ADVANCED;
	grant.flags |= CAPSULE_TPM_ANCHOR_GRANT_TPM_READBACK_VERIFIED;
release:
	if (acquired && tpm_pre_os_lifecycle_end(lifecycle, &token) !=
		CB_SUCCESS)
		goto handoff;
	acquired = false;
handoff:
	if (tpm_pre_os_lifecycle_state(lifecycle) == TPM_PRE_OS_AVAILABLE &&
	    tpm_pre_os_lifecycle_handoff(lifecycle) == CB_SUCCESS)
		handed_off = true;
	if (!handed_off || grant.flags !=
		(CAPSULE_TPM_ANCHOR_GRANT_MEDIA_PREPARED |
		 CAPSULE_TPM_ANCHOR_GRANT_TPM_ADVANCED |
		 CAPSULE_TPM_ANCHOR_GRANT_TPM_READBACK_VERIFIED) ||
	    !inputs_unchanged(binding, &binding_snapshot, authorization,
		&authorization_snapshot, provider, &provider_snapshot))
		goto out;
	grant.flags |= CAPSULE_TPM_ANCHOR_GRANT_TPM_RELEASED;
	install_argument = build_grant(&binding_snapshot, &authorization_snapshot,
		CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS);
	if (memcmp(&grant, &install_argument, sizeof(grant)) ||
	    capsule_tpm_anchor_grant_validate(&install_argument,
		&binding_snapshot) !=
		CB_SUCCESS ||
	    provider_snapshot.install(provider_snapshot.context, &install_argument,
		&binding_snapshot) != CB_SUCCESS)
		goto out;
	result = CB_SUCCESS;
out:
	if (lifecycle && tpm_pre_os_lifecycle_state(lifecycle) ==
		TPM_PRE_OS_AVAILABLE)
		(void)tpm_pre_os_lifecycle_handoff(lifecycle);
	memset(&value, 0, sizeof(value));
	memset(&token, 0, sizeof(token));
	memset(&grant, 0, sizeof(grant));
	memset(&prepared_argument, 0, sizeof(prepared_argument));
	memset(&install_argument, 0, sizeof(install_argument));
	memset(&binding_snapshot, 0, sizeof(binding_snapshot));
	memset(&authorization_snapshot, 0, sizeof(authorization_snapshot));
	memset(&provider_snapshot, 0, sizeof(provider_snapshot));
	__atomic_store_n(&transition->control, TRANSITION_FINISHED,
		__ATOMIC_RELEASE);
	return result;
}
