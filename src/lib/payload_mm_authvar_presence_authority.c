/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_presence_authority.h>
#include <boot/payload_mm_authvar_presence_backing.h>
#include <boot/payload_mm_authvar_service.h>
#include <bootmem.h>
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
	PRESENCE_EXECUTING,
	PRESENCE_RESTRICT_REQUESTED,
	PRESENCE_POISON_REQUESTED,
	PRESENCE_RESTRICTING,
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
	uint64_t generation;
	uint64_t sealed_generation;
	uint64_t lifecycle_generation;
	uint64_t closed_generation;
	uint64_t sealed_closed_generation;
	uint32_t phase;
	uint32_t install_attempted;
} presence;

#if ENV_TEST
static payload_mm_authvar_presence_restrict_test_hook_fn restrict_test_hook;
static payload_mm_authvar_presence_restrict_test_hook_fn restrict_claim_test_hook;
static payload_mm_authvar_presence_restrict_test_hook_fn dispatch_finish_test_hook;
static payload_mm_authvar_presence_restrict_test_hook_fn cleanup_test_hook;
#endif

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

static void restriction_scrub(void)
{
	capability_scrub();
	scrub(presence.context, sizeof(presence.context));
	scrub(presence.sealed_context, sizeof(presence.sealed_context));
	presence.generation = 0;
	presence.sealed_generation = 0;
}

static bool restriction_scrubbed(void)
{
	return presence.generation == 0 && presence.sealed_generation == 0 &&
		bytes_zero(presence.capability, sizeof(presence.capability)) &&
		bytes_zero(presence.sealed_capability,
			sizeof(presence.sealed_capability)) &&
		bytes_zero(presence.context, sizeof(presence.context)) &&
		bytes_zero(presence.sealed_context,
			sizeof(presence.sealed_context));
}

static bool install_gate_sealed(void)
{
	return __atomic_load_n(&presence.install_attempted, __ATOMIC_ACQUIRE) == 1;
}

static bool install_phase_empty(void)
{
	return __atomic_load_n(&presence.phase, __ATOMIC_ACQUIRE) == PRESENCE_EMPTY;
}

static void mailbox_scrub(
	const struct payload_mm_authvar_presence_policy *policy)
{
	scrub((void *)(uintptr_t)policy->backing.base, policy->backing.bytes);
}

static bool backing_valid(
	const struct payload_mm_authvar_presence_policy *policy)
{
	const struct payload_mm_authvar_presence_backing *backing =
		&policy->backing;
	const struct lb_authvar_presence_endpoint *endpoint = &policy->endpoint;

	return backing->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION &&
		backing->size == sizeof(*backing) && backing->base &&
		backing->bytes == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE &&
		backing->generation == endpoint->generation &&
		backing->tag == BM_MEM_RESERVED && !backing->reserved &&
		backing->base <= UINTPTR_MAX - backing->bytes &&
		!(backing->base % PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT) &&
		endpoint->communication_base == backing->base &&
		endpoint->communication_size == PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE &&
		endpoint->communication_size <= backing->bytes;
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
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE);
}

static bool page_guard(
	const struct payload_mm_authvar_presence_policy *snapshot);
static __noreturn void cleanup_fail_stop(
	payload_mm_authvar_presence_fail_stop_fn callback,
	const void *failure_context, size_t failure_context_size);

enum cb_err payload_mm_authvar_presence_authority_install(
	const struct payload_mm_authvar_presence_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context)
{
	struct payload_mm_authvar_presence_policy candidate;
	struct payload_mm_authvar_presence_policy snapshot;
	uint8_t capability[LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE] = { 0 };
	uint8_t failure_context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX] = { 0 };
	payload_mm_authvar_presence_fail_stop_fn failure_callback;
	uint32_t expected = 0;
	enum cb_err result;

	if (!install_phase_empty())
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&presence.install_attempted, &expected, 1,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (!install_phase_empty())
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
		CB_SUCCESS || !backing_valid(&candidate) || !candidate.provision ||
	    !candidate.dma_protected ||
	    !candidate.cpu_rendezvous_active || !candidate.cold_reset ||
	    !candidate.fail_stop ||
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
	    !callback_protected(storage_is_protected, storage_context,
		(const void *)(uintptr_t)candidate.fail_stop) ||
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
	if (payload_mm_authvar_presence_backing_evidence_take(
		&candidate.backing) != CB_SUCCESS ||
	    memcmp(&candidate, trusted_policy, sizeof(candidate)) ||
	    !policy_equal()) {
		restriction_scrub();
		__atomic_store_n(&presence.phase, PRESENCE_POISONED,
			__ATOMIC_RELEASE);
		return CB_ERR;
	}
	snapshot = presence.sealed;
	failure_callback = snapshot.fail_stop;
	if (snapshot.context_size)
		memcpy(failure_context, presence.sealed_context,
			snapshot.context_size);
	if (!page_guard(&snapshot))
		cleanup_fail_stop(failure_callback, failure_context,
			snapshot.context_size);
	result = presence.sealed.provision(presence.sealed.context,
		presence.sealed.endpoint.generation, capability);
	if (result != CB_SUCCESS || bytes_zero(capability, sizeof(capability)) ||
	    memcmp(&candidate, trusted_policy, sizeof(candidate)) ||
	    !policy_equal() || !page_guard(&snapshot)) {
		scrub(capability, sizeof(capability));
		if (!page_guard(&snapshot))
			cleanup_fail_stop(failure_callback, failure_context,
				snapshot.context_size);
		mailbox_scrub(&snapshot);
		restriction_scrub();
		scrub(failure_context, sizeof(failure_context));
		scrub(&snapshot, sizeof(snapshot));
		scrub(&candidate, sizeof(candidate));
		__atomic_store_n(&presence.phase, PRESENCE_POISONED,
			__ATOMIC_RELEASE);
		return CB_ERR;
	}
	memcpy(presence.capability, capability, sizeof(capability));
	memcpy(presence.sealed_capability, capability, sizeof(capability));
	presence.generation = candidate.endpoint.generation;
	presence.sealed_generation = candidate.endpoint.generation;
	__atomic_store_n(&presence.lifecycle_generation,
		candidate.endpoint.generation, __ATOMIC_RELEASE);
	scrub(capability, sizeof(capability));
	scrub(failure_context, sizeof(failure_context));
	scrub(&snapshot, sizeof(snapshot));
	scrub(&candidate, sizeof(candidate));
	__atomic_store_n(&presence.phase, PRESENCE_OPEN, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_authority_restrict(
	uint64_t generation)
{
	struct payload_mm_authvar_presence_policy snapshot;
	uint8_t failure_context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX] = { 0 };
	uint32_t phase;
	uint32_t expected;
	bool clean, valid;

	for (;;) {
		phase = __atomic_load_n(&presence.phase, __ATOMIC_ACQUIRE);
		if (phase == PRESENCE_CLOSED) {
			if (generation && presence.closed_generation == generation &&
			    presence.sealed_closed_generation == generation &&
			    __atomic_load_n(&presence.lifecycle_generation,
				    __ATOMIC_ACQUIRE) == generation &&
			    presence.policy.endpoint.generation == generation &&
			    presence.sealed.endpoint.generation == generation &&
			    install_gate_sealed() && policy_equal() &&
			    restriction_scrubbed())
				return CB_SUCCESS;
			expected = PRESENCE_CLOSED;
			(void)__atomic_compare_exchange_n(&presence.phase, &expected,
				PRESENCE_POISONED, false, __ATOMIC_ACQ_REL,
				__ATOMIC_ACQUIRE);
			return CB_ERR;
		}
		if (phase == PRESENCE_RESTRICTING ||
		    phase == PRESENCE_RESTRICT_REQUESTED ||
		    phase == PRESENCE_POISON_REQUESTED)
			return CB_ERR;
		valid = generation && presence.generation == generation &&
			presence.sealed_generation == generation &&
			__atomic_load_n(&presence.lifecycle_generation,
				__ATOMIC_ACQUIRE) == generation &&
			install_gate_sealed() && policy_equal() &&
			backing_valid(&presence.sealed);
		if (phase == PRESENCE_EXECUTING) {
			expected = PRESENCE_EXECUTING;
#if ENV_TEST
			if (restrict_claim_test_hook)
				restrict_claim_test_hook();
#endif
			if (!__atomic_compare_exchange_n(&presence.phase, &expected,
				valid ? PRESENCE_RESTRICT_REQUESTED :
					PRESENCE_POISON_REQUESTED,
				false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
				continue;
			return CB_ERR;
		}
		if (phase != PRESENCE_OPEN && phase != PRESENCE_ATTEMPTED)
			return CB_ERR;
		expected = phase;
#if ENV_TEST
		if (restrict_claim_test_hook)
			restrict_claim_test_hook();
#endif
		if (__atomic_compare_exchange_n(&presence.phase, &expected,
			PRESENCE_RESTRICTING, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			break;
	}

#if ENV_TEST
	if (restrict_test_hook)
		restrict_test_hook();
#endif
	snapshot = presence.sealed;
	if (snapshot.context_size)
		memcpy(failure_context, presence.sealed_context,
			snapshot.context_size);
	valid = generation && presence.generation == generation &&
		presence.sealed_generation == generation &&
		__atomic_load_n(&presence.lifecycle_generation,
			__ATOMIC_ACQUIRE) == generation &&
		presence.policy.endpoint.generation == generation &&
		presence.sealed.endpoint.generation == generation &&
		install_gate_sealed() && policy_equal() &&
		backing_valid(&snapshot);
	capability_scrub();
	clean = page_guard(&snapshot);
	if (!clean)
		cleanup_fail_stop(snapshot.fail_stop, failure_context,
			snapshot.context_size);
	if (valid) {
		presence.closed_generation = generation;
		presence.sealed_closed_generation = generation;
	} else {
		presence.closed_generation = 0;
		presence.sealed_closed_generation = 0;
	}
	mailbox_scrub(&snapshot);
	restriction_scrub();
#if ENV_TEST
	if (cleanup_test_hook)
		cleanup_test_hook();
#endif
	scrub(failure_context, sizeof(failure_context));
	scrub(&snapshot, sizeof(snapshot));
	expected = PRESENCE_RESTRICTING;
	if (!valid) {
		__atomic_compare_exchange_n(&presence.phase, &expected,
			PRESENCE_POISONED, false, __ATOMIC_RELEASE,
			__ATOMIC_ACQUIRE);
		return CB_ERR;
	}
	if (!__atomic_compare_exchange_n(&presence.phase, &expected,
		PRESENCE_CLOSED, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		return CB_ERR;
	return CB_SUCCESS;
}

void payload_mm_authvar_presence_authority_close(void)
{
	(void)payload_mm_authvar_presence_authority_restrict(
		__atomic_load_n(&presence.lifecycle_generation, __ATOMIC_ACQUIRE));
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

static bool page_guard(
	const struct payload_mm_authvar_presence_policy *snapshot)
{
	bool valid;

	if (!snapshot || !backing_valid(snapshot) || !policy_equal() ||
	    memcmp(snapshot, &presence.sealed, sizeof(*snapshot)))
		return false;
	valid = snapshot->dma_protected(snapshot->context,
		snapshot->backing.base, snapshot->backing.bytes);
	if (!valid || !policy_equal() ||
	    memcmp(snapshot, &presence.sealed, sizeof(*snapshot)))
		return false;
	valid = snapshot->cpu_rendezvous_active(snapshot->context);
	return valid && policy_equal() &&
		!memcmp(snapshot, &presence.sealed, sizeof(*snapshot));
}

static __noreturn void cleanup_fail_stop(
	payload_mm_authvar_presence_fail_stop_fn callback,
	const void *failure_context, size_t failure_context_size)
{
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX] = { 0 };

	if (!callback || failure_context_size > sizeof(context))
		__builtin_trap();
	if (failure_context_size)
		memcpy(context, failure_context, failure_context_size);
	restriction_scrub();
	__atomic_store_n(&presence.phase, PRESENCE_POISONED, __ATOMIC_RELEASE);
	callback(failure_context_size ? context : NULL);
	__builtin_trap();
}

static bool dispatch_close(
	const struct payload_mm_authvar_presence_policy *snapshot,
	uint32_t from, bool exact)
{
	const uint64_t generation = presence.sealed_generation;
	uint8_t failure_context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX] = { 0 };
	uint32_t expected = from;
	bool clean, valid;

	if (!__atomic_compare_exchange_n(&presence.phase, &expected,
		PRESENCE_RESTRICTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	valid = exact && generation && presence.generation == generation &&
		__atomic_load_n(&presence.lifecycle_generation,
			__ATOMIC_ACQUIRE) == generation && install_gate_sealed() &&
		policy_equal() && !memcmp(snapshot, &presence.sealed,
			sizeof(*snapshot));
	capability_scrub();
	if (snapshot->context_size)
		memcpy(failure_context, presence.sealed_context,
			snapshot->context_size);
	clean = page_guard(snapshot);
	if (!clean)
		cleanup_fail_stop(snapshot->fail_stop, failure_context,
			snapshot->context_size);
	if (valid) {
		presence.closed_generation = generation;
		presence.sealed_closed_generation = generation;
	} else {
		presence.closed_generation = 0;
		presence.sealed_closed_generation = 0;
	}
	mailbox_scrub(snapshot);
	restriction_scrub();
#if ENV_TEST
	if (cleanup_test_hook)
		cleanup_test_hook();
#endif
	scrub(failure_context, sizeof(failure_context));
	scrub((void *)snapshot, sizeof(*snapshot));
	expected = PRESENCE_RESTRICTING;
	(void)__atomic_compare_exchange_n(&presence.phase, &expected,
		valid ? PRESENCE_CLOSED : PRESENCE_POISONED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
	return true;
}

static void dispatch_finish(
	const struct payload_mm_authvar_presence_policy *snapshot,
	bool response_live, bool close)
{
	uint32_t state;
	uint32_t expected;

	for (;;) {
		state = __atomic_load_n(&presence.phase, __ATOMIC_ACQUIRE);
		if (state == PRESENCE_RESTRICT_REQUESTED) {
			if (dispatch_close(snapshot, state, true))
				return;
			continue;
		}
		if (state == PRESENCE_POISON_REQUESTED) {
			if (dispatch_close(snapshot, state, false))
				return;
			continue;
		}
		if (state != PRESENCE_EXECUTING)
			return;
		if (close || !response_live) {
#if ENV_TEST
			if (dispatch_finish_test_hook)
				dispatch_finish_test_hook();
#endif
			if (dispatch_close(snapshot, state, true))
				return;
			continue;
		}
		expected = PRESENCE_EXECUTING;
#if ENV_TEST
		if (dispatch_finish_test_hook)
			dispatch_finish_test_hook();
#endif
		if (__atomic_compare_exchange_n(&presence.phase, &expected,
			PRESENCE_ATTEMPTED, false, __ATOMIC_RELEASE,
			__ATOMIC_ACQUIRE))
			return;
	}
}

static enum cb_err reset_or_failstop(
	const struct payload_mm_authvar_presence_policy *policy,
	uint8_t reset_context[PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX])
{
	payload_mm_authvar_presence_cold_reset_fn callback = policy->cold_reset;
	const size_t context_size = policy->context_size;

	dispatch_finish(policy, false, true);
	callback(context_size ? reset_context : NULL);
	scrub(reset_context, PAYLOAD_MM_AUTHVAR_PRESENCE_CONTEXT_MAX);
	{
		uint32_t expected = PRESENCE_CLOSED;

		(void)__atomic_compare_exchange_n(&presence.phase, &expected,
			PRESENCE_POISONED, false, __ATOMIC_RELEASE,
			__ATOMIC_ACQUIRE);
	}
	scrub((void *)policy, sizeof(*policy));
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
	bool response_live = false;
	uint32_t expected = PRESENCE_OPEN;
	uint64_t status;

	if (!__atomic_compare_exchange_n(&presence.phase, &expected,
		PRESENCE_EXECUTING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		return CB_ERR;
	}
	policy = presence.sealed;
	if (policy.context_size)
		memcpy(reset_context, presence.sealed_context, policy.context_size);
	memcpy(capability, presence.sealed_capability, sizeof(capability));
	capability_scrub();
	if (!policy_equal() ||
	    memcmp(&policy, &presence.sealed, sizeof(policy)) ||
	    bytes_zero(capability, sizeof(capability))) {
		scrub(capability, sizeof(capability));
		cleanup_fail_stop(policy.fail_stop, reset_context,
			policy.context_size);
	}
	if (!page_guard(&policy)) {
		scrub(capability, sizeof(capability));
		cleanup_fail_stop(policy.fail_stop, reset_context,
			policy.context_size);
	}
	mailbox = (const void *)(uintptr_t)policy.endpoint.communication_base;
	memcpy(&request, mailbox, sizeof(request));
	if (!policy_equal() || !page_guard(&policy)) {
		scrub(&request, sizeof(request));
		scrub(capability, sizeof(capability));
		cleanup_fail_stop(policy.fail_stop, reset_context,
			policy.context_size);
	}
	if (payload_mm_authvar_presence_request_validate(&policy.endpoint,
		&request, sizeof(request)) != CB_SUCCESS) {
		scrub(&request, sizeof(request));
		scrub(capability, sizeof(capability));
		scrub(reset_context, sizeof(reset_context));
		dispatch_finish(&policy, false, false);
		scrub(&policy, sizeof(policy));
		return CB_ERR;
	}
	if (!capability_equal(request.capability, capability)) {
		if (page_guard(&policy)) {
			publish_completion(&policy, &request,
				PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SECURITY_VIOLATION);
			response_live = true;
		}
		scrub(&request, sizeof(request));
		scrub(capability, sizeof(capability));
		scrub(reset_context, sizeof(reset_context));
		dispatch_finish(&policy, response_live, false);
		scrub(&policy, sizeof(policy));
		return CB_ERR;
	}
	scrub(capability, sizeof(capability));
	status = closed_status(payload_mm_authvar_executor_enter_setup_mode(
		&reset_required));
	if (status == PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_SUCCESS &&
	    !reset_required)
		status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_DEVICE_ERROR;
	if (!policy_equal() || !page_guard(&policy)) {
		scrub(&request, sizeof(request));
		cleanup_fail_stop(policy.fail_stop, reset_context,
			policy.context_size);
	}
	publish_completion(&policy, &request, status);
	response_live = true;
	scrub(&request, sizeof(request));
	if (reset_required)
		return reset_or_failstop(&policy, reset_context);
	scrub(reset_context, sizeof(reset_context));
	dispatch_finish(&policy, response_live, false);
	scrub(&policy, sizeof(policy));
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
	restrict_test_hook = NULL;
	restrict_claim_test_hook = NULL;
	dispatch_finish_test_hook = NULL;
	cleanup_test_hook = NULL;
}

const void *payload_mm_authvar_presence_authority_test_state(size_t *size)
{
	if (size)
		*size = sizeof(presence);
	return &presence;
}

void payload_mm_authvar_presence_authority_restrict_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook)
{
	restrict_test_hook = hook;
}

void payload_mm_authvar_presence_authority_restrict_claim_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook)
{
	restrict_claim_test_hook = hook;
}

void payload_mm_authvar_presence_authority_dispatch_finish_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook)
{
	dispatch_finish_test_hook = hook;
}

void payload_mm_authvar_presence_authority_cleanup_test_hook(
	payload_mm_authvar_presence_restrict_test_hook_fn hook)
{
	cleanup_test_hook = hook;
}
#endif
