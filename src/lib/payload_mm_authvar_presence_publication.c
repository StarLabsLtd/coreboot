/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/coreboot_tables.h>
#if !ENV_TEST
#include <bootstate.h>
#include <halt.h>
#endif
#include <string.h>

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
	void *const original = buffer;
	const size_t original_size = size;
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
	struct lb_header *header)
{
	struct payload_mm_authvar_presence_composition composition = { 0 };
	struct lb_authvar_presence_endpoint endpoint = { 0 };
	struct lb_authvar_presence_endpoint *record;
	enum cb_err status = CB_ERR;
	uint32_t state;

	state = __atomic_load_n(&publication_state, __ATOMIC_ACQUIRE);
	if (state == PUBLICATION_DISABLED)
		return CB_SUCCESS;
	if (state == PUBLICATION_FINALIZING)
		return CB_ERR;
	if (state == PUBLICATION_PUBLISHED || state == PUBLICATION_FAILED)
		return CB_ERR;
	if (!header)
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
	    !claim(PUBLICATION_BUSY, PUBLICATION_FINALIZING))
		goto out;
	if (payload_mm_authvar_presence_producer_publication_take(&endpoint) !=
	    CB_SUCCESS ||
	    !claim(PUBLICATION_FINALIZING, PUBLICATION_PUBLISHED))
		goto out;
	/* No callback or fallible operation is permitted after this commit. */
	record = (void *)lb_new_record(header);
	*record = endpoint;
	status = CB_SUCCESS;
out:
	scrub(&composition, sizeof(composition));
	scrub(&endpoint, sizeof(endpoint));
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
