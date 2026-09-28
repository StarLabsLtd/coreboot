/* SPDX-License-Identifier: GPL-2.0-only */

#include "payload_mm_authvar_presence_lifecycle_close_endpoint_internal.h"
#include <boot/coreboot_tables.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#if !ENV_TEST
#include <bootstate.h>
#include <halt.h>
#endif
#include <string.h>

enum publication_state { PUBLICATION_EMPTY, PUBLICATION_RESERVED,
	PUBLICATION_BUSY, PUBLICATION_COMMITTED, PUBLICATION_FAILED };
static uint32_t publication_state;

__weak bool platform_payload_mm_authvar_presence_lifecycle_close_ready_receipt(
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt)
{
	if (receipt)
		memset(receipt, 0, sizeof(*receipt));
	return false;
}

static bool claim(uint32_t from, uint32_t to)
{
	return __atomic_compare_exchange_n(&publication_state, &from, to, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;
	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static enum cb_err fail_owned(void)
{
	uint32_t expected = PUBLICATION_BUSY;
	if (!__atomic_compare_exchange_n(&publication_state, &expected,
		PUBLICATION_FAILED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	payload_mm_authvar_presence_lifecycle_close_backing_abort();
	return CB_ERR;
}

static bool add_overflows(uintptr_t left, size_t right, uintptr_t *sum)
{
	if (right > UINTPTR_MAX - left)
		return true;
	*sum = left + right;
	return false;
}

static bool endpoint_fits(const struct lb_header *header, uintptr_t table_end,
	struct lb_authvar_presence_lifecycle_close_endpoint **slot)
{
	const struct lb_record *last;
	uintptr_t cursor;
	u32 committed_bytes;

	if (!header || !IS_ALIGNED((uintptr_t)header, LB_ENTRY_ALIGN) ||
	    header->header_bytes != sizeof(*header) ||
	    header->table_entries == UINT32_MAX ||
	    !IS_ALIGNED(header->table_bytes, LB_ENTRY_ALIGN) ||
	    add_overflows((uintptr_t)header, sizeof(*header), &cursor) ||
	    cursor > table_end || add_overflows(cursor, header->table_bytes, &cursor) ||
	    cursor > table_end)
		return false;
	if (header->table_entries) {
		if (table_end - cursor < sizeof(*last))
			return false;
		last = (const void *)cursor;
		if (last->size < sizeof(*last) || !IS_ALIGNED(last->size, LB_ENTRY_ALIGN) ||
		    last->size > UINT32_MAX - header->table_bytes ||
		    add_overflows(cursor, last->size, &cursor) || cursor > table_end)
			return false;
		committed_bytes = header->table_bytes + last->size;
	} else if (header->table_bytes) {
		return false;
	} else {
		committed_bytes = 0;
	}
	if (committed_bytes > UINT32_MAX -
	    sizeof(struct lb_authvar_presence_lifecycle_close_endpoint))
		return false;
	if (slot)
		*slot = (void *)cursor;
	return !add_overflows(cursor,
		sizeof(struct lb_authvar_presence_lifecycle_close_endpoint), &cursor) &&
		cursor <= table_end;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_endpoint_reserve(void)
{
	if (!claim(PUBLICATION_EMPTY, PUBLICATION_BUSY))
		return CB_ERR;
	if (payload_mm_authvar_presence_lifecycle_close_backing_reserve() != CB_SUCCESS)
		return fail_owned();
	if (!claim(PUBLICATION_BUSY, PUBLICATION_RESERVED))
		return fail_owned();
	return CB_SUCCESS;
}

enum cb_err lb_add_payload_mm_authvar_presence_lifecycle_close_endpoint(
	struct lb_header *header, uintptr_t table_end)
{
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt receipt = { 0 };
	struct lb_authvar_presence_lifecycle_close_endpoint endpoint;
	struct lb_authvar_presence_lifecycle_close_endpoint *record;
	struct lb_authvar_presence_lifecycle_close_endpoint *planned;
	struct lb_header saved_header;
	u8 saved_record[sizeof(*record)];
	enum cb_err status = CB_ERR;

	if (!claim(PUBLICATION_RESERVED, PUBLICATION_BUSY))
		goto done;
	if (!endpoint_fits(header, table_end, &planned))
		goto out;
	if (!platform_payload_mm_authvar_presence_lifecycle_close_ready_receipt(
		&receipt) ||
	    payload_mm_authvar_presence_lifecycle_close_ready_receipt_consume(&receipt,
		&endpoint) != CB_SUCCESS ||
	    payload_mm_authvar_presence_lifecycle_close_endpoint_validate(&endpoint) !=
		CB_SUCCESS)
		goto out;
	saved_header = *header;
	memcpy(saved_record, planned, sizeof(saved_record));
	record = (void *)lb_new_record(header);
	if (record != planned) {
		*header = saved_header;
		memcpy(planned, saved_record, sizeof(saved_record));
		goto out;
	}
	/* Receipt consumption is the readiness boundary; backing alone is not. */
	*record = endpoint;
	if (!claim(PUBLICATION_BUSY, PUBLICATION_COMMITTED) ||
	    payload_mm_authvar_presence_lifecycle_close_publication_commit() !=
		CB_SUCCESS)
		goto committed_failure;
	status = CB_SUCCESS;
	goto done;
committed_failure:
	__atomic_store_n(&publication_state, PUBLICATION_FAILED, __ATOMIC_RELEASE);
#if ENV_TEST
	goto done;
#else
	__builtin_trap();
#endif
out:
	fail_owned();
done:
	scrub(&receipt, sizeof(receipt));
	scrub(&endpoint, sizeof(endpoint));
	return status;
}

#if ENV_TEST
void payload_mm_authvar_presence_lifecycle_close_publication_reset_test(void)
{
	publication_state = PUBLICATION_EMPTY;
	payload_mm_authvar_presence_lifecycle_close_endpoint_reset_test();
}
#else
static void reserve_or_die(void *unused)
{
	(void)unused;
	if (payload_mm_authvar_presence_lifecycle_close_endpoint_reserve() != CB_SUCCESS)
		die("Authenticated-variable presence lifecycle-close reservation failed\n");
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_EXIT, reserve_or_die, NULL);
#endif
