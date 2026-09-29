/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <string.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "Authenticated-variable presence lifecycle-close sender is ramstage-only"
#endif

struct sender_owner {
	uint32_t state;
	uint32_t consumed_sources;
	uint64_t next_request;
	uint64_t generation;
};

struct sender_controls {
	struct payload_mm_authvar_presence_lifecycle_close_message *page;
	uint64_t wire_result;
	uint64_t request_id;
	uint32_t expected;
	uint32_t completion;
	bool triggered;
	bool page_owned;
	bool private_corrupt;
	enum cb_err status;
} __aligned(8);

enum sender_state { SENDER_IDLE, SENDER_BUSY, SENDER_POISONED };

static struct sender_owner owner;

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!object_valid(first, first_size, 1U) ||
	    !object_valid(second, second_size, 1U))
		return true;
	return first_base <= second_base ?
		second_base - first_base < first_size :
		first_base - second_base < second_size;
}

static bool source_valid(uint32_t source)
{
	switch (source) {
	case LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE:
	case LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PAYLOAD_FAILURE_OR_RETURN:
	case LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CLOSED_REPROOF:
		return true;
	default:
		return false;
	}
}

#if ENV_TEST
uint64_t payload_mm_authvar_presence_lifecycle_close_trigger_test(
	uint64_t value);

static uint64_t trigger(uint64_t value)
{
	return payload_mm_authvar_presence_lifecycle_close_trigger_test(value);
}
#else
static uint64_t trigger(uint64_t value)
{
	uint32_t eax = (uint32_t)value;
	uint32_t ecx = (uint32_t)(value >> 32);
	const uint16_t dx = APM_CNT;

	__asm__ __volatile__("outb %%al, %%dx"
		: "+a" (eax), "+c" (ecx)
		: "d" (dx)
		: "memory");
	return ((uint64_t)ecx << 32) | eax;
}
#endif

enum cb_err payload_mm_authvar_presence_lifecycle_close_send(
	const struct lb_authvar_presence_lifecycle_close_endpoint *endpoint,
	uint32_t source)
{
	struct payload_mm_authvar_presence_lifecycle_close_message request
		__aligned(8) = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_message response
		__aligned(8) = { 0 };
	struct payload_mm_authvar_presence_lifecycle_close_message observed
		__aligned(8) = { 0 };
	struct lb_authvar_presence_lifecycle_close_endpoint frozen;
	struct sender_owner owner_before;
	struct sender_owner owner_frozen;
	struct sender_controls control = {
		.expected = SENDER_IDLE,
		.status = CB_ERR,
	};

	if (!source_valid(source) || !object_valid(endpoint, sizeof(*endpoint),
		_Alignof(uint32_t)) ||
	    payload_mm_authvar_presence_lifecycle_close_endpoint_validate(endpoint) !=
		CB_SUCCESS ||
	    endpoint->trigger_address != APM_CNT ||
	    endpoint->trigger_value !=
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE ||
	    !__atomic_compare_exchange_n(&owner.state, &control.expected, SENDER_BUSY, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	owner_before = owner;
	frozen = *endpoint;
	if (payload_mm_authvar_presence_lifecycle_close_endpoint_validate(&frozen) !=
		CB_SUCCESS || frozen.trigger_address != APM_CNT ||
	    frozen.trigger_value !=
		SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE ||
	    memcmp(endpoint, &frozen, sizeof(frozen)))
		goto out;
	control.page = (void *)(uintptr_t)frozen.communication_base;
	if (!object_valid(control.page,
		frozen.communication_size, _Alignof(*control.page)) ||
	    overlap(endpoint, sizeof(*endpoint), control.page,
		frozen.communication_size) ||
	    overlap(&endpoint, sizeof(endpoint), control.page,
		frozen.communication_size) ||
	    overlap(&source, sizeof(source), control.page,
		frozen.communication_size) ||
	    overlap(&control, sizeof(control), control.page,
		frozen.communication_size) ||
	    overlap(&owner, sizeof(owner), control.page, frozen.communication_size) ||
	    overlap(&request, sizeof(request), control.page, frozen.communication_size) ||
	    overlap(&response, sizeof(response), control.page, frozen.communication_size) ||
	    overlap(&observed, sizeof(observed), control.page, frozen.communication_size) ||
	    overlap(&frozen, sizeof(frozen), control.page, frozen.communication_size) ||
	    overlap(&owner_before, sizeof(owner_before), control.page,
		frozen.communication_size) ||
	    overlap(&owner_frozen, sizeof(owner_frozen), control.page,
		frozen.communication_size) ||
	    memcmp(endpoint, &frozen, sizeof(frozen)) ||
	    (owner.generation && owner.generation != frozen.generation) ||
	    (owner.consumed_sources & source))
		goto out;
	control.page_owned = true;
	owner.generation = frozen.generation;
	control.request_id = ++owner.next_request;
	if (!control.request_id) {
		control.private_corrupt = true;
		__atomic_store_n(&owner.state, SENDER_POISONED, __ATOMIC_RELEASE);
		goto out;
	}
	request = (struct payload_mm_authvar_presence_lifecycle_close_message) {
		.revision =
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MESSAGE_REVISION,
		.size = sizeof(request),
		.source = source,
		.generation = frozen.generation,
		.request_id = control.request_id,
		.status = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_PENDING,
		.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING,
	};
	if (payload_mm_authvar_presence_lifecycle_close_request_validate(&frozen,
		&request, sizeof(request)) != CB_SUCCESS)
		goto out;
	owner_frozen = owner;
	scrub(control.page, frozen.communication_size);
	*control.page = request;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	__atomic_store_n(&control.page->completion,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING,
		__ATOMIC_RELEASE);
	if (memcmp(&owner, &owner_frozen, sizeof(owner))) {
		control.private_corrupt = true;
		__atomic_store_n(&owner.state, SENDER_POISONED, __ATOMIC_RELEASE);
		goto out;
	}
	if (memcmp(endpoint, &frozen, sizeof(frozen)) ||
	    memcmp(control.page, &request, sizeof(request)))
		goto out;
	control.wire_result = trigger(
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SENTINEL);
	control.triggered = true;
	control.expected = SENDER_BUSY;
	if (!__atomic_compare_exchange_n(&owner.state, &control.expected,
		SENDER_POISONED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
	owner_frozen.state = SENDER_POISONED;
	control.completion = __atomic_load_n(&control.page->completion, __ATOMIC_ACQUIRE);
	if (control.completion != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE)
		goto out;
	response = *control.page;
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	observed = *control.page;
	if (__atomic_load_n(&control.page->completion, __ATOMIC_ACQUIRE) != control.completion ||
	    memcmp(&response, &observed, sizeof(response)) ||
	    memcmp(&owner, &owner_frozen, sizeof(owner)) ||
	    memcmp(endpoint, &frozen, sizeof(frozen)) ||
	    payload_mm_authvar_presence_lifecycle_close_response_validate(&frozen,
		&request, &response, sizeof(response)) != CB_SUCCESS ||
	    response.status !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS ||
	    control.wire_result != PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_WIRE_SUCCESS)
		goto out;
	owner.consumed_sources |= source;
	control.status = CB_SUCCESS;
out:
	if (control.page_owned)
		scrub(control.page, frozen.communication_size);
	scrub(&request, sizeof(request));
	scrub(&response, sizeof(response));
	scrub(&observed, sizeof(observed));
	scrub(&frozen, sizeof(frozen));
	if (control.status == CB_SUCCESS)
		__atomic_store_n(&owner.state, SENDER_IDLE, __ATOMIC_RELEASE);
	else if (!control.triggered && !control.private_corrupt) {
		owner.generation = owner_before.generation;
		owner.next_request = owner_before.next_request;
		owner.consumed_sources = owner_before.consumed_sources;
		control.expected = SENDER_BUSY;
		(void)__atomic_compare_exchange_n(&owner.state, &control.expected,
			SENDER_IDLE, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
	}
	scrub(&owner_before, sizeof(owner_before));
	scrub(&owner_frozen, sizeof(owner_frozen));
	{
		enum cb_err status = control.status;
		scrub(&control, sizeof(control));
		return status;
	}
}

#if ENV_TEST
void payload_mm_authvar_presence_lifecycle_close_sender_reset_test(void)
{
	scrub(&owner, sizeof(owner));
}
#endif
