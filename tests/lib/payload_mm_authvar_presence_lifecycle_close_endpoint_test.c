/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_publication.h>
#include <bootmem.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint_internal.h"

#define assert(condition) do { if (!(condition)) abort(); } while (0)

static uint8_t page[4096] __aligned(4096);
static uint8_t table[sizeof(struct lb_header) + 64] __aligned(16);
static struct bootmem_aligned_reservation_request saved_request;
static struct payload_mm_authvar_presence_lifecycle_close_ready_receipt ready;
static bool provide;
static bool reservation_error;
static struct lb_header *race_header;
static enum cb_err race_status[2];
static struct payload_mm_authvar_presence_lifecycle_close_backing provider_backing;
static uint32_t provider_phase;

int bootmem_aligned_reservation_register(
	const struct bootmem_aligned_reservation_request *request,
	struct bootmem_aligned_reservation_handle *handle)
{
	saved_request = *request;
	handle->opaque[0] = 1;
	return reservation_error ? -1 : 0;
}

int bootmem_aligned_reservation_query(
	const struct bootmem_aligned_reservation_handle *handle,
	struct bootmem_aligned_reservation *reservation)
{
	assert(handle->opaque[0] == 1);
	*reservation = (struct bootmem_aligned_reservation) {
		.base = (uintptr_t)page, .size = sizeof(page), .tag = BM_MEM_RESERVED,
	};
	return 0;
}

bool platform_payload_mm_authvar_presence_lifecycle_close_ready_receipt(
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt)
{
	if (!provide)
		return false;
	*receipt = ready;
	return true;
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	struct lb_record *record = (void *)((uint8_t *)header + sizeof(*header) +
		header->table_bytes);
	if (header->table_entries) {
		header->table_bytes += record->size;
		record = (void *)((uint8_t *)header + sizeof(*header) +
			header->table_bytes);
	}
	header->table_entries++;
	record->tag = LB_TAG_UNUSED;
	record->size = sizeof(*record);
	return record;
}

static struct lb_authvar_presence_lifecycle_close_endpoint endpoint(void)
{
	return (struct lb_authvar_presence_lifecycle_close_endpoint) {
		.tag = LB_TAG_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ENDPOINT,
		.size = 64, .revision = 1, .header_size = 64,
		.flags = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_REQUIRED_FLAGS,
		.generation = 7, .communication_base = (uintptr_t)page,
		.communication_size = 64, .message_size = 64,
		.transport = 1, .trigger_width = 1,
		.trigger_address = 0xb2, .trigger_value = 0xe3,
		.source_mask = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_SOURCE_MASK,
	};
}

static struct payload_mm_authvar_presence_lifecycle_close_message request(void)
{
	return (struct payload_mm_authvar_presence_lifecycle_close_message) {
		.revision = 1, .size = 64,
		.source = LB_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PRE_EXTERNAL_IMAGE,
		.generation = 7, .request_id = 9,
		.status = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_PENDING,
		.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_PENDING,
	};
}

#define REJECT_ENDPOINT(member, value) do { \
	struct lb_authvar_presence_lifecycle_close_endpoint bad = endpoint(); \
	bad.member = (value); \
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_validate(&bad) == CB_ERR); \
} while (0)

static void test_abi(void)
{
	uint8_t overlap[128] __aligned(8);
	uint8_t misaligned[sizeof(struct lb_authvar_presence_lifecycle_close_endpoint) + 8]
		__aligned(8);
	struct lb_authvar_presence_lifecycle_close_endpoint ep = endpoint();
	struct payload_mm_authvar_presence_lifecycle_close_message in = request();
	struct payload_mm_authvar_presence_lifecycle_close_message out = in;
	const uint64_t statuses[] = {
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_INVALID_PARAMETER,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_UNSUPPORTED,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_DEVICE_ERROR,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_ACCESS_DENIED,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SECURITY_VIOLATION,
	};
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_validate(&ep) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
		(const void *)(misaligned + 1)) == CB_ERR);
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
		(const void *)(UINTPTR_MAX - 31U)) == CB_ERR);
	REJECT_ENDPOINT(tag, LB_TAG_AUTHVAR_PRESENCE_ENDPOINT);
	REJECT_ENDPOINT(size, 63);
	REJECT_ENDPOINT(revision, 2);
	REJECT_ENDPOINT(header_size, 63);
	REJECT_ENDPOINT(flags, ep.flags ^ 1);
	REJECT_ENDPOINT(generation, 0);
	REJECT_ENDPOINT(communication_base, ep.communication_base + 8);
	REJECT_ENDPOINT(communication_size, 4096);
	REJECT_ENDPOINT(message_size, 63);
	REJECT_ENDPOINT(transport, 0);
	REJECT_ENDPOINT(trigger_width, 2);
	REJECT_ENDPOINT(trigger_address, 0);
	REJECT_ENDPOINT(trigger_value, 0);
	REJECT_ENDPOINT(source_mask, ep.source_mask ^ 1);
	assert(payload_mm_authvar_presence_lifecycle_close_request_validate(&ep,
		&in, sizeof(in)) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_request_validate(&ep,
		(const void *)(misaligned + 1), sizeof(in)) == CB_ERR);
	assert(payload_mm_authvar_presence_lifecycle_close_request_validate(&ep,
		(const void *)(UINTPTR_MAX & ~(uintptr_t)7U), sizeof(in)) == CB_ERR);
	for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); i++) {
		out = in;
		out.status = statuses[i];
		out.completion = 0;
		assert(payload_mm_authvar_presence_lifecycle_close_response_validate(
			&ep, &in, &out, sizeof(out)) == CB_SUCCESS);
	}
	out.status = 1;
	assert(payload_mm_authvar_presence_lifecycle_close_response_validate(
		&ep, &in, &out, sizeof(out)) == CB_ERR);
	memcpy(overlap, &in, sizeof(in));
	assert(payload_mm_authvar_presence_lifecycle_close_response_validate(&ep,
		overlap, overlap, sizeof(in)) == CB_ERR);
	assert(payload_mm_authvar_presence_lifecycle_close_response_validate(&ep,
		overlap, overlap + 8, sizeof(in)) == CB_ERR);
	assert(payload_mm_authvar_presence_lifecycle_close_response_validate(&ep,
		&in, (const void *)(misaligned + 1), sizeof(in)) == CB_ERR);
	assert(payload_mm_authvar_presence_lifecycle_close_response_validate(&ep,
		&in, (const void *)(UINTPTR_MAX & ~(uintptr_t)7U), sizeof(in)) == CB_ERR);
	in.source = 3;
	assert(payload_mm_authvar_presence_lifecycle_close_request_validate(&ep,
		&in, sizeof(in)) == CB_ERR);
	in = request(); in.reserved[4] = 1;
	assert(payload_mm_authvar_presence_lifecycle_close_request_validate(&ep,
		&in, sizeof(in)) == CB_ERR);
}

static void *complete_thread(void *argument)
{
	struct payload_mm_authvar_presence_lifecycle_close_message *response = argument;
	response->status = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_STATUS_SUCCESS;
	__atomic_store_n(&response->completion,
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_COMPLETE, __ATOMIC_RELEASE);
	return NULL;
}

static void test_completion_publication(void)
{
	pthread_t thread;
	struct lb_authvar_presence_lifecycle_close_endpoint ep = endpoint();
	struct payload_mm_authvar_presence_lifecycle_close_message in = request();
	struct payload_mm_authvar_presence_lifecycle_close_message out = in;
	assert(!pthread_create(&thread, NULL, complete_thread, &out));
	while (payload_mm_authvar_presence_lifecycle_close_response_validate(&ep,
		&in, &out, sizeof(out)) != CB_SUCCESS)
		;
	assert(!pthread_join(thread, NULL));
}

static void prepare_ready(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_backing backing;
	memset(page, 0xa5, sizeof(page));
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_reserve() ==
		CB_SUCCESS);
	assert(saved_request.bytes == 4096 && saved_request.alignment == 4096 &&
		saved_request.limit_exclusive == (1ULL << 32) &&
		saved_request.tag == BM_MEM_RESERVED);
	assert(payload_mm_authvar_presence_lifecycle_close_backing_take(&backing) ==
		CB_SUCCESS);
	for (size_t i = 0; i < sizeof(page); i++)
		assert(page[i] == 0);
	ready = (struct payload_mm_authvar_presence_lifecycle_close_ready_receipt) {
		.revision = 1, .size = sizeof(ready), .endpoint = endpoint(),
		.identity = 1, .nonce = 2, .active = 1,
	};
	assert(payload_mm_authvar_presence_lifecycle_close_backing_return(&backing,
		&ready) == CB_SUCCESS);
}

static void test_publication(void)
{
	struct lb_header *header = (void *)table;
	payload_mm_authvar_presence_lifecycle_close_publication_reset_test();
	memset(table, 0, sizeof(table));
	header->header_bytes = sizeof(*header);
	prepare_ready();
	provide = true;
	assert(lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(header,
		(uintptr_t)table + sizeof(table)) == CB_SUCCESS);
	assert(header->table_entries == 1);
	assert(!memcmp(table + sizeof(*header), &ready.endpoint,
		sizeof(ready.endpoint)));
	assert(lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(header,
		(uintptr_t)table + sizeof(table)) == CB_ERR);

	payload_mm_authvar_presence_lifecycle_close_publication_reset_test();
	memset(table, 0, sizeof(table)); header->header_bytes = sizeof(*header);
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_reserve() ==
		CB_SUCCESS);
	provide = true;
	assert(lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(header,
		(uintptr_t)table + sizeof(table)) == CB_ERR);

	/* A missing ready receipt rolls back and scrubs provider-owned backing. */
	payload_mm_authvar_presence_lifecycle_close_publication_reset_test();
	memset(table, 0, sizeof(table)); header->header_bytes = sizeof(*header);
	prepare_ready();
	memset(page, 0x5a, sizeof(page));
	provide = false;
	assert(lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(header,
		(uintptr_t)table + sizeof(table)) == CB_ERR);
	for (size_t i = 0; i < sizeof(page); i++)
		assert(page[i] == 0);
	provide = true;
	assert(lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(header,
		(uintptr_t)table + sizeof(table)) == CB_ERR);
}

static void test_receipt_binding(void)
{
	struct lb_header *header = (void *)table;
	for (unsigned int mutation = 0; mutation < 4; mutation++) {
		payload_mm_authvar_presence_lifecycle_close_publication_reset_test();
		memset(table, 0, sizeof(table));
		header->header_bytes = sizeof(*header);
		prepare_ready();
		if (mutation == 0)
			ready.endpoint.generation++;
		else if (mutation == 1)
			ready.identity++;
		else if (mutation == 2)
			ready.nonce++;
		else
			ready.reserved = 1;
		provide = true;
		assert(lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(
			header, (uintptr_t)table + sizeof(table)) == CB_ERR);
		assert(header->table_entries == 0);
	}
}

static void *provider_thread(void *unused)
{
	(void)unused;
	assert(payload_mm_authvar_presence_lifecycle_close_backing_take(
		&provider_backing) == CB_SUCCESS);
	memset(page, 0xa5, sizeof(page));
	__atomic_store_n(&provider_phase, 1, __ATOMIC_RELEASE);
	while (__atomic_load_n(&provider_phase, __ATOMIC_ACQUIRE) != 2)
		;
	assert(payload_mm_authvar_presence_lifecycle_close_backing_return(
		&provider_backing, &ready) == CB_ERR);
	return NULL;
}

static void test_provider_abort(void)
{
	pthread_t thread;
	payload_mm_authvar_presence_lifecycle_close_publication_reset_test();
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_reserve() ==
		CB_SUCCESS);
	ready = (struct payload_mm_authvar_presence_lifecycle_close_ready_receipt) {
		.revision = 1, .size = sizeof(ready), .endpoint = endpoint(),
		.identity = 1, .nonce = 2, .active = 1,
	};
	provider_phase = 0;
	assert(!pthread_create(&thread, NULL, provider_thread, NULL));
	while (__atomic_load_n(&provider_phase, __ATOMIC_ACQUIRE) != 1)
		;
	payload_mm_authvar_presence_lifecycle_close_backing_abort();
	/* Externally owned backing cannot be scrubbed until return acknowledges. */
	assert(page[0] == 0xa5);
	__atomic_store_n(&provider_phase, 2, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	for (size_t i = 0; i < sizeof(page); i++)
		assert(page[i] == 0);
}

static void test_reservation_exhaustion(void)
{
	payload_mm_authvar_presence_lifecycle_close_publication_reset_test();
	reservation_error = true;
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_reserve() == CB_ERR);
	reservation_error = false;
	assert(payload_mm_authvar_presence_lifecycle_close_endpoint_reserve() == CB_ERR);
}

static void *publish_thread(void *argument)
{
	const uintptr_t index = (uintptr_t)argument;
	race_status[index] = lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(
		race_header, (uintptr_t)table + sizeof(table));
	return NULL;
}

static void test_contention(void)
{
	pthread_t threads[2];
	payload_mm_authvar_presence_lifecycle_close_publication_reset_test();
	memset(table, 0, sizeof(table));
	race_header = (void *)table;
	race_header->header_bytes = sizeof(*race_header);
	prepare_ready();
	provide = true;
	assert(!pthread_create(&threads[0], NULL, publish_thread, (void *)0));
	assert(!pthread_create(&threads[1], NULL, publish_thread, (void *)1));
	assert(!pthread_join(threads[0], NULL));
	assert(!pthread_join(threads[1], NULL));
	assert((race_status[0] == CB_SUCCESS) != (race_status[1] == CB_SUCCESS));
	assert(race_header->table_entries == 1);
}

int main(void)
{
	test_abi();
	test_completion_publication();
	test_publication();
	test_receipt_binding();
	test_contention();
	test_provider_abort();
	test_reservation_exhaustion();
	return 0;
}
