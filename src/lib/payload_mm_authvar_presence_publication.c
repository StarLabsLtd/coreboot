/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <boot/coreboot_tables.h>
#if !ENV_TEST
#include <bootstate.h>
#include <halt.h>
#endif
#include <string.h>

#include "payload_mm_authvar_presence_lifecycle_close_provider_internal.h"

enum publication_state {
	PUBLICATION_EMPTY,
	PUBLICATION_BUSY,
	PUBLICATION_DISABLED,
	PUBLICATION_RESERVED,
	PUBLICATION_FINALIZING,
	PUBLICATION_PUBLISHED,
	PUBLICATION_FAILED,
};

static uint32_t publication_state;

#if ENV_TEST
__weak void payload_mm_authvar_presence_publication_scrub_test_hook(
	const void *buffer, size_t size)
{
	(void)buffer;
	(void)size;
}

__weak void payload_mm_authvar_presence_publication_pre_poison_test_hook(
	uint32_t observed_state)
{
	(void)observed_state;
}

__weak void payload_mm_authvar_presence_publication_pre_reserve_claim_test_hook(void)
{
}
#endif

static __noinline void scrub(void *buffer, size_t size)
{
#if ENV_TEST
	void *const original = buffer;
	const size_t original_size = size;
#endif
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
#if ENV_TEST
	payload_mm_authvar_presence_publication_scrub_test_hook(original,
		original_size);
#endif
}

__weak bool platform_payload_mm_authvar_presence_required(void)
{
	return false;
}

__weak bool platform_payload_mm_authvar_presence_composition(
	struct payload_mm_authvar_presence_composition *composition)
{
	if (composition)
		memset(composition, 0, sizeof(*composition));
	return false;
}

static bool claim(uint32_t from, uint32_t to)
{
	return __atomic_compare_exchange_n(&publication_state, &from, to, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static bool add_overflows(uintptr_t left, size_t right, uintptr_t *sum)
{
	if (right > UINTPTR_MAX - left)
		return true;
	*sum = left + right;
	return false;
}

static bool endpoint_fits(const struct lb_header *header, uintptr_t table_end,
			  struct lb_authvar_presence_endpoint **slot)
{
	const struct lb_record *last;
	uintptr_t cursor;
	u32 committed_bytes;

	if (!header || !IS_ALIGNED((uintptr_t)header, LB_ENTRY_ALIGN) ||
	    header->header_bytes != sizeof(*header) ||
	    header->table_entries > UINT32_MAX -
		(CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT) ?
		 2U : 1U) ||
	    !IS_ALIGNED(header->table_bytes, LB_ENTRY_ALIGN) ||
	    add_overflows((uintptr_t)header, sizeof(*header), &cursor) ||
	    cursor > table_end ||
	    add_overflows(cursor, header->table_bytes, &cursor) ||
	    cursor > table_end)
		return false;

	if (header->table_entries) {
		if (table_end - cursor < sizeof(*last))
			return false;
		last = (const void *)cursor;
		if (last->size < sizeof(*last) ||
		    !IS_ALIGNED(last->size, LB_ENTRY_ALIGN) ||
		    last->size > UINT32_MAX - header->table_bytes ||
		    add_overflows(cursor, last->size, &cursor) ||
		    cursor > table_end)
			return false;
		committed_bytes = header->table_bytes + last->size;
	} else if (header->table_bytes) {
		return false;
	} else {
		committed_bytes = 0;
	}
	if (committed_bytes >
	    UINT32_MAX - sizeof(struct lb_authvar_presence_endpoint) -
		(CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT) ?
		 sizeof(struct lb_authvar_presence_lifecycle_close_endpoint) : 0U))
		return false;

	if (slot)
		*slot = (void *)cursor;
	return !add_overflows(cursor,
			     sizeof(struct lb_authvar_presence_endpoint) +
			     (CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT) ?
			      sizeof(struct lb_authvar_presence_lifecycle_close_endpoint) : 0U),
			     &cursor) &&
	       cursor <= table_end;
}

static __noreturn void
committed_publication_fail_stop(struct payload_mm_authvar_presence_receipt *receipt)
{
	payload_mm_authvar_presence_producer_publication_fail_stop(receipt);
	__builtin_unreachable();
}

static enum cb_err fail(void)
{
	uint32_t state = __atomic_load_n(&publication_state, __ATOMIC_ACQUIRE);

	for (;;) {
		if (state == PUBLICATION_DISABLED || state == PUBLICATION_PUBLISHED ||
		    state == PUBLICATION_FAILED || state == PUBLICATION_FINALIZING)
			return CB_ERR;
#if ENV_TEST
		payload_mm_authvar_presence_publication_pre_poison_test_hook(state);
#endif
		if (__atomic_compare_exchange_n(&publication_state, &state,
			PUBLICATION_FAILED, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE)) {
			payload_mm_authvar_presence_producer_abort();
			return CB_ERR;
		}
	}
}

enum cb_err payload_mm_authvar_presence_publication_reserve(void)
{
	bool required;

	if (!claim(PUBLICATION_EMPTY, PUBLICATION_BUSY))
		return fail();
	required = platform_payload_mm_authvar_presence_required();
	if (__atomic_load_n(&publication_state, __ATOMIC_ACQUIRE) !=
	    PUBLICATION_BUSY)
		return fail();
	if (!required) {
		if (!claim(PUBLICATION_BUSY, PUBLICATION_DISABLED))
			return fail();
		return CB_SUCCESS;
	}
	if (payload_mm_authvar_presence_producer_reserve() != CB_SUCCESS ||
	    !claim(PUBLICATION_BUSY, PUBLICATION_RESERVED))
		return fail();
	return CB_SUCCESS;
}

enum cb_err lb_add_payload_mm_authvar_presence_endpoint(
	struct lb_header *header, uintptr_t table_end)
{
	struct payload_mm_authvar_presence_composition composition = { 0 };
	struct lb_authvar_presence_endpoint *record;
	struct lb_authvar_presence_endpoint *planned_record;
	struct payload_mm_authvar_presence_receipt receipt = { 0 };
	u8 saved_record[sizeof(*record)];
	u8 reserved_record[sizeof(*record)];
	struct lb_header saved_header;
	struct lb_header reserved_header;
	enum cb_err status = CB_ERR;
	uint32_t state;

	state = __atomic_load_n(&publication_state, __ATOMIC_ACQUIRE);
	if (state == PUBLICATION_DISABLED)
		return CB_SUCCESS;
	if (state == PUBLICATION_FINALIZING)
		return CB_ERR;
	if (state == PUBLICATION_PUBLISHED || state == PUBLICATION_FAILED)
		return CB_ERR;
	if (!endpoint_fits(header, table_end, NULL))
		return fail();
#if ENV_TEST
	payload_mm_authvar_presence_publication_pre_reserve_claim_test_hook();
#endif
	if (!claim(PUBLICATION_RESERVED, PUBLICATION_BUSY))
		return fail();
	if (!platform_payload_mm_authvar_presence_composition(&composition) ||
	    __atomic_load_n(&publication_state, __ATOMIC_ACQUIRE) !=
		PUBLICATION_BUSY ||
	    payload_mm_authvar_presence_producer_compose(&composition) !=
		CB_SUCCESS ||
	    !endpoint_fits(header, table_end, &planned_record) ||
	    !claim(PUBLICATION_BUSY, PUBLICATION_FINALIZING))
		goto out;
	saved_header = *header;
	memcpy(saved_record, planned_record, sizeof(saved_record));
	record = (void *)lb_new_record(header);
	if (record != planned_record) {
		memcpy(planned_record, saved_record, sizeof(saved_record));
		*header = saved_header;
		payload_mm_authvar_presence_producer_abort();
		__atomic_store_n(&publication_state, PUBLICATION_FAILED,
				 __ATOMIC_RELEASE);
		goto out;
	}
	reserved_header = *header;
	memcpy(reserved_record, record, sizeof(reserved_record));
	if (payload_mm_authvar_presence_producer_publication_take(&receipt) !=
	    CB_SUCCESS) {
		memcpy(record, saved_record, sizeof(saved_record));
		*header = saved_header;
		goto out;
	}
	if (CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_TRANSPORT) &&
	    payload_mm_authvar_presence_lifecycle_close_provider_prepare(&receipt) !=
		CB_SUCCESS)
		committed_publication_fail_stop(&receipt);
	/* No callback or fallible operation is permitted after this commit. */
	if (memcmp(header, &reserved_header, sizeof(*header)) ||
	    memcmp(record, reserved_record, sizeof(reserved_record)))
		committed_publication_fail_stop(&receipt);
	*record = receipt.endpoint;
	if (!claim(PUBLICATION_FINALIZING, PUBLICATION_PUBLISHED))
		committed_publication_fail_stop(&receipt);
	if (payload_mm_authvar_presence_producer_publication_complete(&receipt) !=
	    CB_SUCCESS)
		committed_publication_fail_stop(&receipt);
	status = CB_SUCCESS;
out:
	scrub(&composition, sizeof(composition));
	scrub(&receipt, sizeof(receipt));
	if (status != CB_SUCCESS) {
		if (__atomic_load_n(&publication_state, __ATOMIC_ACQUIRE) ==
		    PUBLICATION_FINALIZING) {
			uint32_t expected = PUBLICATION_FINALIZING;
			(void)__atomic_compare_exchange_n(&publication_state, &expected,
				PUBLICATION_FAILED, false, __ATOMIC_RELEASE,
				__ATOMIC_ACQUIRE);
			return CB_ERR;
		}
		return fail();
	}
	return CB_SUCCESS;
}

#if ENV_TEST
void payload_mm_authvar_presence_publication_reset_test(void)
{
	__atomic_store_n(&publication_state, PUBLICATION_EMPTY, __ATOMIC_RELEASE);
}
#else
static void reserve_or_die(void *unused)
{
	(void)unused;
	if (payload_mm_authvar_presence_publication_reserve() != CB_SUCCESS)
		die("Authenticated-variable presence reservation failed\n");
}

BOOT_STATE_INIT_ENTRY(BS_PRE_DEVICE, BS_ON_EXIT, reserve_or_die, NULL);
#endif
