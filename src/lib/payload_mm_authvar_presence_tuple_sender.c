/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <cpu/x86/smm_invocation_tuple_trigger.h>
#include <string.h>

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

static bool overlaps(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t a = (uintptr_t)first;
	const uintptr_t b = (uintptr_t)second;

	if (!object_valid(first, first_size, 1U) ||
	    !object_valid(second, second_size, 1U))
		return true;
	return a <= b ? b - a < first_size : a - b < second_size;
}

static bool physical_overlap(uintptr_t page, uint64_t base, uint64_t size)
{
	if (!size || base > UINT64_MAX - (size - 1U))
		return size != 0;
	return (uint64_t)page <= base ?
		base - page < PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE :
		(uint64_t)page - base < size;
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static enum cb_err send(void *context,
	const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	uint32_t decision,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	struct payload_mm_authvar_presence_tuple_sender sender;
	struct payload_mm_authvar_presence_transaction_binding binding_snapshot;
	struct payload_mm_authvar_presence_seed seed_snapshot;
	struct payload_mm_authvar_presence_transaction_request request = { 0 };
	struct payload_mm_authvar_presence_transaction_request response_request = { 0 };
	struct payload_mm_authvar_presence_transaction_request response_request_check;
	struct payload_mm_authvar_presence_transaction_ack response_ack = { 0 };
	struct payload_mm_authvar_presence_transaction_ack response_ack_check;
	struct payload_mm_authvar_presence_transaction_page *page;
	uint64_t value = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL;
	bool prepare = decision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	bool response_valid;
	enum cb_err status = CB_ERR;

	if (!object_valid(context, sizeof(sender), _Alignof(typeof(sender))) ||
	    !object_valid(binding, sizeof(*binding), _Alignof(*binding)) ||
	    !object_valid(ack, sizeof(*ack), _Alignof(*ack)) ||
	    !object_valid(saved_value, sizeof(*saved_value),
		_Alignof(*saved_value)) ||
	    (prepare &&
	     !object_valid(seed, sizeof(*seed), _Alignof(*seed))) ||
	    (!prepare && seed))
		return CB_ERR;
	sender = *(const struct payload_mm_authvar_presence_tuple_sender *)context;
	page = sender.page;
	if (!object_valid(page, sizeof(*page), _Alignof(*page)) ||
	    overlaps(context, sizeof(sender), binding, sizeof(*binding)) ||
	    overlaps(context, sizeof(sender), ack, sizeof(*ack)) ||
	    overlaps(context, sizeof(sender), saved_value, sizeof(*saved_value)) ||
	    overlaps(binding, sizeof(*binding), ack, sizeof(*ack)) ||
	    overlaps(binding, sizeof(*binding), saved_value, sizeof(*saved_value)) ||
	    overlaps(ack, sizeof(*ack), saved_value, sizeof(*saved_value)) ||
	    overlaps(page, sizeof(*page), context, sizeof(sender)) ||
	    overlaps(page, sizeof(*page), binding, sizeof(*binding)) ||
	    overlaps(page, sizeof(*page), ack, sizeof(*ack)) ||
	    overlaps(page, sizeof(*page), saved_value, sizeof(*saved_value)) ||
	    (prepare &&
	     (overlaps(seed, sizeof(*seed), context, sizeof(sender)) ||
	      overlaps(seed, sizeof(*seed), binding, sizeof(*binding)) ||
	      overlaps(seed, sizeof(*seed), ack, sizeof(*ack)) ||
	      overlaps(seed, sizeof(*seed), saved_value, sizeof(*saved_value)) ||
	      overlaps(seed, sizeof(*seed), page, sizeof(*page)))))
		return CB_ERR;
	memset(ack, 0, sizeof(*ack));
	*saved_value = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL;
	if (sender.revision !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER_REVISION ||
	    sender.size != sizeof(sender))
		return CB_ERR;
	binding_snapshot = *binding;
	memset(&seed_snapshot, 0, sizeof(seed_snapshot));
	if (prepare) {
		seed_snapshot = *seed;
		if (seed_snapshot.revision !=
			PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION ||
		    seed_snapshot.size != sizeof(seed_snapshot) ||
		    seed_snapshot.endpoint.generation != binding_snapshot.generation ||
		    seed_snapshot.backing.generation != binding_snapshot.generation ||
		    physical_overlap((uintptr_t)page,
			seed_snapshot.endpoint.communication_base,
			seed_snapshot.endpoint.communication_size) ||
		    physical_overlap((uintptr_t)page, seed_snapshot.backing.base,
			seed_snapshot.backing.bytes)) {
			scrub(&seed_snapshot, sizeof(seed_snapshot));
			scrub(&binding_snapshot, sizeof(binding_snapshot));
			return CB_ERR;
		}
	}
	if (!payload_mm_authvar_presence_transaction_result(&binding_snapshot,
		decision)) {
		scrub(&seed_snapshot, sizeof(seed_snapshot));
		scrub(&binding_snapshot, sizeof(binding_snapshot));
		return CB_ERR;
	}
	request.binding = binding_snapshot;
	request.decision = decision;
	if (prepare)
		request.seed = seed_snapshot;
	scrub(page, sizeof(*page));
	page->request = request;
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (memcmp(context, &sender, sizeof(sender)) ||
	    memcmp(binding, &binding_snapshot, sizeof(binding_snapshot)) ||
	    (prepare && memcmp(seed, &seed_snapshot, sizeof(seed_snapshot)))) {
		scrub(page, sizeof(*page));
		goto out;
	}
	value = smm_invocation_tuple_trigger();
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	response_request = page->request;
	response_ack = page->ack;
	response_valid = zero(&response_request, sizeof(response_request)) &&
		zero(page->reserved, sizeof(page->reserved));
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
	response_request_check = page->request;
	response_ack_check = page->ack;
	response_valid = response_valid &&
		!memcmp(&response_request, &response_request_check,
			sizeof(response_request)) &&
		!memcmp(&response_ack, &response_ack_check, sizeof(response_ack)) &&
		zero(page->reserved, sizeof(page->reserved));
	scrub(page, sizeof(*page));
	response_valid = response_valid &&
		!memcmp(context, &sender, sizeof(sender)) &&
		!memcmp(binding, &binding_snapshot, sizeof(binding_snapshot)) &&
		(!prepare || !memcmp(seed, &seed_snapshot, sizeof(seed_snapshot)));
	if (response_valid &&
	    payload_mm_authvar_presence_transaction_ack_valid(&binding_snapshot,
		decision, &response_ack, value)) {
		*ack = response_ack;
		*saved_value = value;
		status = CB_SUCCESS;
		goto out;
	}
	if (prepare && response_valid &&
	    payload_mm_authvar_presence_transaction_ack_valid(&binding_snapshot,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT,
		&response_ack, value)) {
		*ack = response_ack;
		*saved_value = value;
	}
out:
	scrub(&response_request, sizeof(response_request));
	scrub(&response_request_check, sizeof(response_request_check));
	scrub(&response_ack, sizeof(response_ack));
	scrub(&response_ack_check, sizeof(response_ack_check));
	scrub(&request, sizeof(request));
	scrub(&seed_snapshot, sizeof(seed_snapshot));
	scrub(&binding_snapshot, sizeof(binding_snapshot));
	scrub(&value, sizeof(value));
	return status;
}

enum cb_err payload_mm_authvar_presence_tuple_sender_prepare(
	void *context, const struct payload_mm_authvar_presence_seed *seed,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	return send(context, seed, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, ack, saved_value);
}

enum cb_err payload_mm_authvar_presence_tuple_sender_commit(
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	return send(context, NULL, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, ack, saved_value);
}

enum cb_err payload_mm_authvar_presence_tuple_sender_abort(
	void *context,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct payload_mm_authvar_presence_transaction_ack *ack,
	uint64_t *saved_value)
{
	return send(context, NULL, binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, ack, saved_value);
}
