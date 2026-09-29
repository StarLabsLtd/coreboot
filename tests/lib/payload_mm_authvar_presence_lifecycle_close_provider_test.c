/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/lib/payload_mm_authvar_presence_lifecycle_close_endpoint_internal.h"
#include "../../src/lib/payload_mm_authvar_presence_lifecycle_close_provider_internal.h"
#include "../../src/lib/payload_mm_authvar_presence_producer_internal.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

enum install_mode { INSTALL_SUCCESS, INSTALL_CLEAN_ERROR,
	INSTALL_DIRTY_ERROR, INSTALL_BAD_RECEIPT };

static uint8_t backing_page[
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE]
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_ALIGNMENT);
static struct payload_mm_authvar_presence_receipt presence;
static jmp_buf failure;
static enum install_mode mode;
static unsigned int install_calls;
static unsigned int cancel_calls;
static unsigned int return_calls;
static uint64_t random_value;

bool payload_mm_authvar_presence_producer_publication_receipt_validate(
	const struct payload_mm_authvar_presence_receipt *receipt)
{
	return receipt == &presence && receipt->active == 1 &&
		receipt->endpoint.generation == 9;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_take(
	struct payload_mm_authvar_presence_lifecycle_close_backing *backing)
{
	*backing = (struct payload_mm_authvar_presence_lifecycle_close_backing) {
		.base = (uintptr_t)backing_page,
		.bytes = sizeof(backing_page),
		.tag = 0,
	};
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_return(
	const struct payload_mm_authvar_presence_lifecycle_close_backing *backing,
	const struct payload_mm_authvar_presence_lifecycle_close_ready_receipt *receipt)
{
	return_calls++;
	return backing && receipt && backing->base == (uintptr_t)backing_page &&
		receipt->endpoint.communication_base == backing->base ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_presence_lifecycle_close_backing_cancel(
	const struct payload_mm_authvar_presence_lifecycle_close_backing *backing)
{
	assert(backing && backing->base == (uintptr_t)backing_page);
	cancel_calls++;
	return CB_SUCCESS;
}

void payload_mm_authvar_presence_lifecycle_close_backing_abort(void)
{
	abort();
}

enum cb_err get_random_number_64(uint64_t *value)
{
	*value = ++random_value;
	return CB_SUCCESS;
}

enum cb_err
platform_payload_mm_authvar_presence_lifecycle_close_route_install(
	const struct payload_mm_authvar_presence_lifecycle_close_install_descriptor
		*descriptor,
	struct payload_mm_authvar_presence_lifecycle_close_install_receipt *receipt)
{
	install_calls++;
	if (mode == INSTALL_DIRTY_ERROR) {
		((struct payload_mm_authvar_presence_lifecycle_close_install_descriptor *)
		 descriptor)->generation++;
		return CB_ERR;
	}
	if (mode == INSTALL_CLEAN_ERROR)
		return CB_ERR;
	*receipt =
		(struct payload_mm_authvar_presence_lifecycle_close_install_receipt) {
			.descriptor = *descriptor,
			.protected_route_identity = 1,
			.route_nonce = 2,
			.installed = mode == INSTALL_BAD_RECEIPT ? 0 : 1,
		};
	return CB_SUCCESS;
}

void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
	longjmp(failure, 1);
}

static void reset(enum install_mode next_mode)
{
	payload_mm_authvar_presence_lifecycle_close_provider_reset_test();
	memset(backing_page, 0, sizeof(backing_page));
	memset(&presence, 0, sizeof(presence));
	presence.endpoint.generation = 9;
	presence.active = 1;
	mode = next_mode;
	install_calls = 0;
	cancel_calls = 0;
	return_calls = 0;
	random_value = 0;
}

int main(void)
{
	struct payload_mm_authvar_presence_lifecycle_close_ready_receipt ready;

	reset(INSTALL_SUCCESS);
	assert(payload_mm_authvar_presence_lifecycle_close_provider_prepare(
		&presence) == CB_SUCCESS);
	assert(install_calls == 1 && return_calls == 1 && !cancel_calls);
	assert(platform_payload_mm_authvar_presence_lifecycle_close_ready_receipt(
		&ready));
	assert(ready.endpoint.generation == presence.endpoint.generation);
	assert(!platform_payload_mm_authvar_presence_lifecycle_close_ready_receipt(
		&ready));
	payload_mm_authvar_presence_lifecycle_close_provider_commit();

	reset(INSTALL_CLEAN_ERROR);
	assert(payload_mm_authvar_presence_lifecycle_close_provider_prepare(
		&presence) == CB_ERR);
	assert(install_calls == 1 && cancel_calls == 1 && !return_calls);

	reset(INSTALL_DIRTY_ERROR);
	if (!setjmp(failure)) {
		(void)payload_mm_authvar_presence_lifecycle_close_provider_prepare(
			&presence);
		abort();
	}

	reset(INSTALL_BAD_RECEIPT);
	if (!setjmp(failure)) {
		(void)payload_mm_authvar_presence_lifecycle_close_provider_prepare(
			&presence);
		abort();
	}
	return 0;
}
