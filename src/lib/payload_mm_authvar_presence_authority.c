/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_service.h>
#if !ENV_TEST
#include <halt.h>
#endif
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Payload-MM authenticated-variable presence authority is SMM-only"
#endif

enum presence_phase {
	PRESENCE_EMPTY,
	PRESENCE_OPEN,
	PRESENCE_ATTEMPTED,
	PRESENCE_CLOSED,
	PRESENCE_POISONED,
};

static struct {
	struct payload_mm_authvar_presence_policy policy;
	struct payload_mm_authvar_presence_policy sealed;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX];
	uint8_t sealed_context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX];
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
	uint8_t sealed_capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
	uint32_t phase;
	uint32_t install_attempted;
} presence;

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static bool capability_equal(const uint8_t *left, const uint8_t *right)
{
	uint8_t different = 0;

	for (size_t index = 0; index < LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE; index++)
		different |= left[index] ^ right[index];
	return different == 0;
}

static bool context_equal(size_t size)
{
	return !memcmp(presence.context, presence.sealed_context, size);
}

static bool policy_equal(void)
{
	struct payload_mm_authvar_presence_policy policy = presence.policy;
	struct payload_mm_authvar_presence_policy sealed = presence.sealed;

	if (policy.context != (policy.context_size ? presence.context : NULL) ||
	    sealed.context != (sealed.context_size ?
		presence.sealed_context : NULL) ||
	    policy.context_size > sizeof(presence.context) ||
	    policy.context_size != sealed.context_size)
		return false;
	policy.context = NULL;
	sealed.context = NULL;
	return !memcmp(&policy, &sealed, sizeof(policy)) &&
		context_equal(policy.context_size);
}

static void capability_scrub(void)
{
	scrub(presence.capability, sizeof(presence.capability));
	scrub(presence.sealed_capability, sizeof(presence.sealed_capability));
}

static void mailbox_scrub(
	const struct lb_authvar_presence_endpoint *endpoint)
{
	scrub((void *)(uintptr_t)endpoint->communication_base,
		endpoint->communication_size);
}

static bool callback_protected(payload_mm_authvar_protected_storage proof,
	void *context, const void *callback)
{
	return callback && proof(context, callback, 1U);
}

static bool mailbox_unprotected(payload_mm_authvar_protected_storage proof,
	void *context, const struct lb_authvar_presence_endpoint *endpoint)
{
	return !proof(context,
		(const void *)(uintptr_t)endpoint->communication_base,
		endpoint->communication_size);
}

enum cb_err payload_mm_authvar_presence_authority_install(
	const struct payload_mm_authvar_presence_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context)
{
	struct payload_mm_authvar_presence_policy candidate;
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE] = { 0 };
	uint32_t expected = 0;
	enum cb_err result;

	if (!__atomic_compare_exchange_n(&presence.install_attempted, &expected, 1,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (!trusted_policy || !storage_is_protected ||
	    !storage_is_protected(storage_context,
		(const void *)(uintptr_t)storage_is_protected, 1U) ||
	    !storage_is_protected(storage_context, &presence, sizeof(presence)) ||
	    !storage_is_protected(storage_context, trusted_policy,
		sizeof(*trusted_policy)))
		return CB_ERR;
	memcpy(&candidate, trusted_policy, sizeof(candidate));
	if (candidate.revision != PAYLOAD_MM_AUTHVAR_PRESENCE_POLICY_REVISION ||
	    candidate.size != sizeof(candidate) ||
	    payload_mm_authvar_presence_endpoint_validate(&candidate.endpoint) !=
		CB_SUCCESS || !candidate.provision || !candidate.dma_protected ||
	    !candidate.cpu_rendezvous_active || !candidate.cold_reset ||
	    (candidate.context == NULL) != (candidate.context_size == 0U) ||
	    candidate.context_size > PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX ||
	    !mailbox_unprotected(storage_is_protected, storage_context,
		&candidate.endpoint) ||
	    (candidate.context_size &&
	     !storage_is_protected(storage_context, candidate.context,
		candidate.context_size)) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)candidate.provision) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)candidate.dma_protected) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)candidate.cpu_rendezvous_active) ||
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)candidate.cold_reset) ||
	    memcmp(&candidate, trusted_policy, sizeof(candidate)))
		return CB_ERR;
	if (candidate.context_size) {
		memcpy(presence.context, candidate.context, candidate.context_size);
		memcpy(presence.sealed_context, candidate.context,
			candidate.context_size);
	}
	presence.policy = candidate;
	presence.sealed = candidate;
	presence.policy.context = candidate.context_size ? presence.context : NULL;
	presence.sealed.context = candidate.context_size ?
		presence.sealed_context : NULL;
	result = presence.sealed.provision(presence.sealed.context,
		presence.sealed.endpoint.generation, capability);
	if (result != CB_SUCCESS || bytes_zero(capability, sizeof(capability)) ||
	    memcmp(&candidate, trusted_policy, sizeof(candidate)) ||
	    !policy_equal()) {
		scrub(capability, sizeof(capability));
		mailbox_scrub(&candidate.endpoint);
		presence.phase = PRESENCE_POISONED;
		return CB_ERR;
	}
	memcpy(presence.capability, capability, sizeof(capability));
	memcpy(presence.sealed_capability, capability, sizeof(capability));
	scrub(capability, sizeof(capability));
	__atomic_store_n(&presence.phase, PRESENCE_OPEN, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

void payload_mm_authvar_presence_authority_close(void)
{
	uint32_t expected = PRESENCE_OPEN;

	if (__atomic_compare_exchange_n(&presence.phase, &expected,
		PRESENCE_CLOSED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		capability_scrub();
}

static uint64_t closed_status(uint64_t status)
{
	switch (status) {
	case PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS:
		return PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS;
	case PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED:
		return PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_UNSUPPORTED;
	case PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED:
		return PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_WRITE_PROTECTED;
	case PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED:
		return PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_ACCESS_DENIED;
	case PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION:
		return PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION;
	case PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR:
	default:
		return PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR;
	}
}

static void publish_completion(
	const struct payload_mm_authvar_presence_policy *policy,
	const struct payload_mm_authvar_presence_message *request, uint64_t status)
{
	struct payload_mm_authvar_presence_message response = *request;
	struct payload_mm_authvar_presence_message *mailbox =
		(void *)(uintptr_t)policy->endpoint.communication_base;

	response.status = status;
	response.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING;
	memcpy(mailbox, &response, offsetof(
		struct payload_mm_authvar_presence_message, completion));
	__atomic_store_n(&mailbox->completion,
		PAYLOAD_MM_AUTHVAR_PRESENCE_COMPLETE, __ATOMIC_RELEASE);
	scrub(&response, sizeof(response));
}

static bool execution_guard(void)
{
	struct payload_mm_authvar_presence_policy snapshot = presence.sealed;
	bool valid;

	valid = snapshot.dma_protected(snapshot.context,
		snapshot.endpoint.communication_base,
		snapshot.endpoint.communication_size);
	if (!valid || !policy_equal() ||
	    memcmp(&snapshot, &presence.sealed, sizeof(snapshot)))
		return false;
	valid = snapshot.cpu_rendezvous_active(snapshot.context);
	return valid && policy_equal() &&
		!memcmp(&snapshot, &presence.sealed, sizeof(snapshot));
}

static enum cb_err reset_or_failstop(
	const struct payload_mm_authvar_presence_policy *policy,
	uint8_t reset_context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX])
{
	policy->cold_reset(policy->context_size ? reset_context : NULL);
	scrub(reset_context, PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX);
	__atomic_store_n(&presence.phase, PRESENCE_POISONED, __ATOMIC_RELEASE);
#if !ENV_TEST
	halt();
#endif
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_authority_dispatch(void)
{
	struct payload_mm_authvar_presence_message request;
	struct payload_mm_authvar_presence_policy policy;
	const struct payload_mm_authvar_presence_message *mailbox;
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE];
	uint8_t reset_context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX] = { 0 };
	bool reset_required = false;
	uint32_t expected = PRESENCE_OPEN;
	uint64_t status;

	if (!__atomic_compare_exchange_n(&presence.phase, &expected,
		PRESENCE_ATTEMPTED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		return CB_ERR;
	}
	policy = presence.sealed;
	memcpy(capability, presence.sealed_capability, sizeof(capability));
	capability_scrub();
	if (!policy_equal() ||
	    memcmp(&policy, &presence.sealed, sizeof(policy)) ||
	    bytes_zero(capability, sizeof(capability))) {
		scrub(capability, sizeof(capability));
		scrub(reset_context, sizeof(reset_context));
		return CB_ERR;
	}
	if (policy.context_size)
		memcpy(reset_context, policy.context, policy.context_size);
	if (!execution_guard()) {
		scrub(capability, sizeof(capability));
		scrub(reset_context, sizeof(reset_context));
		return CB_ERR;
	}
	mailbox = (const void *)(uintptr_t)policy.endpoint.communication_base;
	memcpy(&request, mailbox, sizeof(request));
	if (!policy_equal() || !execution_guard() ||
	    payload_mm_authvar_presence_request_validate(&policy.endpoint,
		&request, sizeof(request)) != CB_SUCCESS) {
		scrub(&request, sizeof(request));
		scrub(capability, sizeof(capability));
		scrub(reset_context, sizeof(reset_context));
		return CB_ERR;
	}
	if (!capability_equal(request.capability, capability)) {
		if (execution_guard())
			publish_completion(&policy, &request,
				PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION);
		scrub(&request, sizeof(request));
		scrub(capability, sizeof(capability));
		scrub(reset_context, sizeof(reset_context));
		return CB_ERR;
	}
	scrub(capability, sizeof(capability));
	status = closed_status(payload_mm_authvar_executor_enter_setup_mode(
		&reset_required));
	if (status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS &&
	    !reset_required)
		status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR;
	if (!policy_equal()) {
		status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR;
		__atomic_store_n(&presence.phase, PRESENCE_POISONED,
			__ATOMIC_RELEASE);
	}
	if (!execution_guard()) {
		scrub(&request, sizeof(request));
		if (reset_required)
			return reset_or_failstop(&policy, reset_context);
		scrub(reset_context, sizeof(reset_context));
		return CB_ERR;
	}
	publish_completion(&policy, &request, status);
	scrub(&request, sizeof(request));
	if (reset_required)
		return reset_or_failstop(&policy, reset_context);
	scrub(reset_context, sizeof(reset_context));
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_smi_dispatch(uint16_t port,
	uint8_t value)
{
	struct lb_authvar_presence_endpoint endpoint = presence.sealed.endpoint;

	if (endpoint.transport != LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8 ||
	    endpoint.trigger_width != sizeof(value) ||
	    endpoint.trigger_address != port || endpoint.trigger_value != value)
		return CB_ERR;
	return payload_mm_authvar_presence_authority_dispatch();
}

#if ENV_TEST
void payload_mm_authvar_presence_authority_reset_test(void)
{
	scrub(&presence, sizeof(presence));
}

const void *payload_mm_authvar_presence_authority_test_state(size_t *size)
{
	if (size)
		*size = sizeof(presence);
	return &presence;
}
#endif
