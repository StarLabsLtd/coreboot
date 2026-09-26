/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <stdlib.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static unsigned int producer_calls;

enum cb_err payload_mm_authvar_presence_producer_reserve(void)
{
	producer_calls++;
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_producer_compose(
	const struct payload_mm_authvar_presence_composition *composition)
{
	(void)composition;
	producer_calls++;
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_producer_publication_take(
	struct lb_authvar_presence_endpoint *endpoint)
{
	(void)endpoint;
	producer_calls++;
	return CB_ERR;
}

void payload_mm_authvar_presence_producer_abort(void)
{
	producer_calls++;
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	(void)header;
	producer_calls++;
	return NULL;
}

int main(void)
{
	struct lb_header header = { 0 };

	payload_mm_authvar_presence_publication_reset_test();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(lb_add_payload_mm_authvar_presence_endpoint(&header) == CB_SUCCESS);
	assert(producer_calls == 0);
	assert(header.table_entries == 0 && header.table_bytes == 0);
	return 0;
}
