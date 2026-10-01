/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static unsigned int reserve_calls;
static unsigned int abort_calls;
static unsigned int service_calls;
static unsigned int sender_calls;
static unsigned int sender_closes;
static bool service_ready;
static bool reenter;
static enum cb_err reserve_status;
static struct lb_header header;

enum cb_err payload_mm_authvar_presence_producer_reserve(void)
{
	reserve_calls++;
	return reserve_status;
}

enum cb_err payload_mm_authvar_presence_tuple_sender_reserve(void)
{
	sender_calls++;
	return CB_SUCCESS;
}

void payload_mm_authvar_presence_tuple_sender_close(void)
{
	sender_closes++;
}

void payload_mm_authvar_presence_producer_abort(void)
{
	abort_calls++;
}

bool platform_payload_mm_authvar_presence_required(void)
{
	abort();
}

bool platform_payload_mm_authvar_presence_composition(
	struct payload_mm_authvar_presence_composition *composition)
{
	(void)composition;
	abort();
}

enum cb_err payload_mm_authvar_presence_producer_compose(
	const struct payload_mm_authvar_presence_composition *composition)
{
	(void)composition;
	abort();
}

enum cb_err payload_mm_authvar_presence_producer_publication_take(
	struct payload_mm_authvar_presence_receipt *receipt)
{
	(void)receipt;
	abort();
}

enum cb_err payload_mm_authvar_presence_producer_publication_complete(
	struct payload_mm_authvar_presence_receipt *receipt)
{
	(void)receipt;
	abort();
}

void payload_mm_authvar_presence_producer_publication_fail_stop(
	struct payload_mm_authvar_presence_receipt *receipt)
{
	(void)receipt;
	abort();
}

struct lb_record *lb_new_record(struct lb_header *table)
{
	(void)table;
	abort();
}

#ifndef TEST_WEAK_SERVICE_DEFAULT
bool platform_payload_mm_authvar_service_published(
	const struct lb_header *table, uintptr_t table_end)
{
	service_calls++;
	assert(table == &header && table_end == (uintptr_t)&header + sizeof(header));
	if (reenter) {
		reenter = false;
		assert(lb_add_payload_mm_authvar_presence_endpoint(&header, table_end) == CB_ERR);
	}
	return service_ready;
}
#endif

static void reset(void)
{
	payload_mm_authvar_presence_publication_reset_test();
	reserve_calls = abort_calls = service_calls = sender_calls = sender_closes = 0;
	reserve_status = CB_SUCCESS;
	service_ready = reenter = false;
	memset(&header, 0, sizeof(header));
	header.header_bytes = sizeof(header);
}

int main(void)
{
	const uintptr_t table_end = (uintptr_t)&header + sizeof(header);
#ifndef TEST_WEAK_SERVICE_DEFAULT
	struct lb_header saved;
#endif
	bool required = true;

	reset();
	assert(payload_mm_authvar_presence_publication_loader_required(&required) == CB_ERR);
	assert(!required);
	assert(lb_add_payload_mm_authvar_presence_endpoint(&header, table_end) == CB_ERR);
	assert(!service_calls);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(reserve_calls == 1 && sender_calls == 1);
	assert(payload_mm_authvar_presence_publication_loader_required(&required) == CB_SUCCESS);
	assert(required);
	assert(lb_add_payload_mm_authvar_presence_endpoint(&header, table_end) == CB_ERR);
	assert(abort_calls == 1 && sender_closes == 1);
	assert(lb_add_payload_mm_authvar_presence_endpoint(&header, table_end) == CB_ERR);
#ifndef TEST_WEAK_SERVICE_DEFAULT
	assert(service_calls == 1);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	service_ready = true;
	saved = header;
	assert(lb_add_payload_mm_authvar_presence_endpoint(&header, table_end) == CB_SUCCESS);
	assert(!memcmp(&saved, &header, sizeof(header)));
	assert(!abort_calls && !sender_closes && service_calls == 1);
	assert(lb_add_payload_mm_authvar_presence_endpoint(&header, table_end) == CB_ERR);
	assert(service_calls == 1);
	assert(lb_add_payload_mm_authvar_presence_endpoint(NULL, 0) == CB_ERR);
	assert(service_calls == 1);
	assert(payload_mm_authvar_presence_publication_loader_required(&required) == CB_ERR);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	service_ready = reenter = true;
	assert(lb_add_payload_mm_authvar_presence_endpoint(&header, table_end) == CB_ERR);
	assert(abort_calls == 1 && sender_closes == 1);
#endif

	reset();
	reserve_status = CB_ERR;
	assert(payload_mm_authvar_presence_publication_reserve() == CB_ERR);
	assert(reserve_calls == 1 && !sender_calls && !service_calls);
	assert(abort_calls == 1 && sender_closes == 1);
	return 0;
}
