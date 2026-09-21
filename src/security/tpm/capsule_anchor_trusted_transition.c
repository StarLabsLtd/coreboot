/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/capsule_anchor_transition.h>
#include <security/tpm/platform_auth.h>
#include <string.h>

#if ENV_SMM && !ENV_TEST
#error "The trusted capsule TPM transition must not be built in SMM"
#endif

#define TRANSITION_RUNNING 0x43545231U
#define TRANSITION_FINISHED 0x43544631U

struct trusted_transport_context {
	struct tpm_pre_os_lifecycle *lifecycle;
	const struct tpm_pre_os_token *token;
};

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_start = (uintptr_t)left;
	uintptr_t right_start = (uintptr_t)right;

	if (!left || !right || !left_size || !right_size ||
	    left_start > UINTPTR_MAX - left_size ||
	    right_start > UINTPTR_MAX - right_size)
		return true;
	return left_start < right_start + right_size &&
		right_start < left_start + left_size;
}

static bool claim(struct capsule_tpm_anchor_transition *transition)
{
	uint32_t expected = 0;

	return transition && __atomic_compare_exchange_n(&transition->control,
		&expected, TRANSITION_RUNNING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE);
}

static enum cb_err transmit(void *opaque, const uint8_t *request,
	size_t request_size, uint8_t *response, size_t *response_size)
{
	struct trusted_transport_context *context = opaque;

	return tpm_pre_os_lifecycle_transmit(context->lifecycle,
		context->token, request, request_size, response, response_size);
}

static bool provider_valid(
	const struct capsule_tpm_anchor_trusted_transition_provider *provider)
{
	return provider->revision ==
			CAPSULE_TPM_ANCHOR_TRUSTED_TRANSITION_PROVIDER_REVISION &&
		provider->size == sizeof(*provider) && provider->prepared &&
		provider->read && provider->install &&
		(!!provider->context == !!provider->context_size);
}

static bool binding_policy_equal(
	const struct capsule_tpm_anchor_binding *expected,
	const struct capsule_tpm_anchor_binding *observed)
{
	struct capsule_tpm_anchor_binding normalized = *observed;

	normalized.write_locked = expected->write_locked;
	return !memcmp(expected, &normalized, sizeof(*expected));
}

static bool inputs_unchanged(
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_binding *binding_snapshot,
	const struct capsule_tpm_anchor_platform_request *request,
	const struct capsule_tpm_anchor_platform_request *request_snapshot,
	const struct capsule_tpm_anchor_trusted_transition_provider *provider,
	const struct capsule_tpm_anchor_trusted_transition_provider *provider_snapshot)
{
	return !memcmp(binding, binding_snapshot, sizeof(*binding)) &&
		!memcmp(request, request_snapshot, sizeof(*request)) &&
		!memcmp(provider, provider_snapshot, sizeof(*provider));
}

static struct capsule_tpm_anchor_grant build_grant(
	const struct capsule_tpm_anchor_platform_request *request, uint32_t flags)
{
	return (struct capsule_tpm_anchor_grant) {
		.revision = CAPSULE_TPM_ANCHOR_GRANT_REVISION,
		.size = sizeof(struct capsule_tpm_anchor_grant),
		.policy_revision = request->policy_revision,
		.nv_index = request->nv_index,
		.generation = request->generation,
		.transaction = request->transaction,
		.flags = flags,
		.current = request->current,
		.candidate = request->candidate,
	};
}

static enum cb_err read_state(
	const struct capsule_tpm_anchor_trusted_transition_provider *provider,
	struct trusted_transport_context *transport_context,
	const struct capsule_tpm_anchor_binding *binding,
	struct capsule_tpm_anchor_binding *observed,
	struct capsule_tpm_anchor_value *value)
{
	struct capsule_tpm_anchor_binding argument = *binding;

	memset(observed, 0, sizeof(*observed));
	memset(value, 0, sizeof(*value));
	if (provider->read(provider->context, transmit, transport_context,
		&argument, observed, value) != CB_SUCCESS ||
	    memcmp(&argument, binding, sizeof(argument)) ||
	    !capsule_tpm_anchor_platform_binding_valid(observed) ||
	    !binding_policy_equal(binding, observed))
		return CB_ERR;
	return CB_SUCCESS;
}

static bool trusted_inputs_disjoint(
	struct capsule_tpm_anchor_transition *transition,
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_platform_request *request,
	const struct capsule_tpm_anchor_trusted_transition_provider *provider)
{
	const void *objects[] = {
		transition, lifecycle, binding, request, provider, provider->context,
	};
	const size_t sizes[] = {
		sizeof(*transition), sizeof(*lifecycle), sizeof(*binding),
		sizeof(*request), sizeof(*provider), provider->context_size,
	};

	for (size_t left = 0; left < ARRAY_SIZE(objects); left++) {
		if (!sizes[left])
			continue;
		for (size_t right = left + 1; right < ARRAY_SIZE(objects); right++) {
			if (sizes[right] && ranges_overlap(objects[left], sizes[left],
				objects[right], sizes[right]))
				return false;
		}
	}
	return true;
}

enum cb_err capsule_tpm_anchor_transition_run_trusted(
	struct capsule_tpm_anchor_transition *transition,
	struct tpm_pre_os_lifecycle *lifecycle,
	const struct capsule_tpm_anchor_binding *binding,
	const struct capsule_tpm_anchor_platform_request *request,
	const struct capsule_tpm_anchor_trusted_transition_provider *provider)
{
	struct capsule_tpm_anchor_binding binding_snapshot;
	struct capsule_tpm_anchor_binding observed = { 0 };
	struct capsule_tpm_anchor_binding final_binding = { 0 };
	struct capsule_tpm_anchor_platform_request request_snapshot;
	struct capsule_tpm_anchor_trusted_transition_provider provider_snapshot;
	struct capsule_tpm_anchor_value value = { 0 };
	struct capsule_tpm_anchor_grant grant = { 0 };
	struct capsule_tpm_anchor_grant callback_grant = { 0 };
	struct capsule_tpm_anchor_binding callback_binding = { 0 };
	struct tpm2_platform_auth_result command_result = { 0 };
	struct tpm_pre_os_token token = { 0 };
	struct trusted_transport_context transport_context = {
		.lifecycle = lifecycle,
		.token = &token,
	};
	enum cb_err result = CB_ERR;
	bool acquired = false;
	bool transition_valid = false;
	bool close_verified = false;
	bool handed_off = false;

	if (!claim(transition))
		return CB_ERR;
	if (!lifecycle || !binding || !request || !provider)
		goto out;
	memcpy(&binding_snapshot, binding, sizeof(binding_snapshot));
	memcpy(&request_snapshot, request, sizeof(request_snapshot));
	memcpy(&provider_snapshot, provider, sizeof(provider_snapshot));
	if (!provider_valid(&provider_snapshot) ||
	    !trusted_inputs_disjoint(transition, lifecycle, binding, request,
		&provider_snapshot) ||
	    !capsule_tpm_anchor_platform_request_valid(&request_snapshot,
		&binding_snapshot) ||
	    !inputs_unchanged(binding, &binding_snapshot, request,
		&request_snapshot, provider, &provider_snapshot))
		goto out;
	grant = build_grant(&request_snapshot, 0);
	callback_grant = grant;
	if (provider_snapshot.prepared(provider_snapshot.context,
		&callback_grant) != CB_SUCCESS ||
	    memcmp(&callback_grant, &grant, sizeof(grant)) ||
	    !inputs_unchanged(binding, &binding_snapshot, request,
		&request_snapshot, provider, &provider_snapshot))
		goto out;
	grant.flags = CAPSULE_TPM_ANCHOR_GRANT_MEDIA_PREPARED;
	if (tpm_pre_os_lifecycle_acquire(lifecycle, &token) != CB_SUCCESS)
		goto out;
	acquired = true;
	if (read_state(&provider_snapshot, &transport_context, &binding_snapshot,
		&observed, &value) != CB_SUCCESS ||
	    !inputs_unchanged(binding, &binding_snapshot, request,
		&request_snapshot, provider, &provider_snapshot))
		goto cleanup;
	if (!memcmp(&value, &request_snapshot.current, sizeof(value))) {
		if (observed.write_locked)
			goto cleanup;
		(void)tpm2_platform_auth_nv_write(lifecycle, &token,
			binding_snapshot.nv_index,
			(const uint8_t *)&request_snapshot.candidate,
			&command_result);
		if (read_state(&provider_snapshot, &transport_context,
			&binding_snapshot, &observed, &value) != CB_SUCCESS ||
		    memcmp(&value, &request_snapshot.candidate, sizeof(value)) ||
		    observed.write_locked)
			goto cleanup;
	} else if (memcmp(&value, &request_snapshot.candidate, sizeof(value))) {
		goto cleanup;
	}
	if (!observed.write_locked) {
		memset(&command_result, 0, sizeof(command_result));
		(void)tpm2_platform_auth_nv_write_lock(lifecycle, &token,
			binding_snapshot.nv_index, &command_result);
		if (read_state(&provider_snapshot, &transport_context,
			&binding_snapshot, &observed, &value) != CB_SUCCESS ||
		    memcmp(&value, &request_snapshot.candidate, sizeof(value)) ||
		    !observed.write_locked)
			goto cleanup;
	}
	if (memcmp(&value, &request_snapshot.candidate, sizeof(value)) ||
	    !observed.write_locked ||
	    !inputs_unchanged(binding, &binding_snapshot, request,
		&request_snapshot, provider, &provider_snapshot))
		goto cleanup;
	final_binding = observed;
	grant.flags |= CAPSULE_TPM_ANCHOR_GRANT_TPM_ADVANCED |
		CAPSULE_TPM_ANCHOR_GRANT_TPM_READBACK_VERIFIED;
	transition_valid = true;
cleanup:
	if (tpm_pre_os_lifecycle_state(lifecycle) == TPM_PRE_OS_OWNED) {
		memset(&command_result, 0, sizeof(command_result));
		(void)tpm2_platform_auth_close_ph_enable(lifecycle, &token,
			&command_result);
		memset(&command_result, 0, sizeof(command_result));
		if (tpm2_platform_auth_verify_startup_clear(lifecycle, &token,
			&command_result) == CB_SUCCESS)
			close_verified = true;
	}
	if (!close_verified && tpm_pre_os_lifecycle_state(lifecycle) ==
		TPM_PRE_OS_OWNED)
		(void)tpm_pre_os_lifecycle_fail(lifecycle, &token);
	if (close_verified &&
	    tpm_pre_os_lifecycle_end(lifecycle, &token) == CB_SUCCESS) {
		acquired = false;
		if (tpm_pre_os_lifecycle_handoff(lifecycle) == CB_SUCCESS)
			handed_off = true;
	}
	if (!transition_valid || !close_verified || !handed_off ||
	    !inputs_unchanged(binding, &binding_snapshot, request,
		&request_snapshot, provider, &provider_snapshot))
		goto out;
	grant.flags |= CAPSULE_TPM_ANCHOR_GRANT_TPM_RELEASED;
	callback_grant = build_grant(&request_snapshot,
		CAPSULE_TPM_ANCHOR_GRANT_REQUIRED_FLAGS);
	callback_binding = final_binding;
	if (memcmp(&grant, &callback_grant, sizeof(grant)) ||
	    capsule_tpm_anchor_platform_grant_validate(&callback_grant,
		&callback_binding) != CB_SUCCESS ||
	    provider_snapshot.install(provider_snapshot.context, &callback_grant,
		&callback_binding) != CB_SUCCESS ||
	    memcmp(&grant, &callback_grant, sizeof(grant)) ||
	    memcmp(&final_binding, &callback_binding, sizeof(final_binding)) ||
	    !inputs_unchanged(binding, &binding_snapshot, request,
		&request_snapshot, provider, &provider_snapshot))
		goto out;
	result = CB_SUCCESS;
out:
	if (acquired && tpm_pre_os_lifecycle_state(lifecycle) ==
		TPM_PRE_OS_OWNED)
		(void)tpm_pre_os_lifecycle_fail(lifecycle, &token);
	memset(&token, 0, sizeof(token));
	memset(&command_result, 0, sizeof(command_result));
	memset(&observed, 0, sizeof(observed));
	memset(&final_binding, 0, sizeof(final_binding));
	memset(&value, 0, sizeof(value));
	memset(&grant, 0, sizeof(grant));
	memset(&callback_grant, 0, sizeof(callback_grant));
	memset(&callback_binding, 0, sizeof(callback_binding));
	memset(&binding_snapshot, 0, sizeof(binding_snapshot));
	memset(&request_snapshot, 0, sizeof(request_snapshot));
	memset(&provider_snapshot, 0, sizeof(provider_snapshot));
	__atomic_store_n(&transition->control, TRANSITION_FINISHED,
		__ATOMIC_RELEASE);
	return result;
}
