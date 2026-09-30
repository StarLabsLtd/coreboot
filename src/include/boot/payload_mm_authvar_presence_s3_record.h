/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RECORD_H
#define BOOT_PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RECORD_H

#include <boot/payload_mm_authvar_presence.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_endpoint.h>
#include <commonlib/bsd/cb_err.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_PRESENCE_S3_FACTS_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_S3_CLOSED 1U
#define PAYLOAD_MM_AUTHVAR_PRESENCE_S3_LIFECYCLE_CLOSE_IDLE 1U

/*
 * Persistent semantic identity only.  In particular, this structure must not
 * grow pointers, callbacks, capabilities, verifier material, bootmem handles,
 * or an image of live route state.
 */
struct payload_mm_authvar_presence_s3_facts {
	uint32_t revision;
	uint32_t size;
	struct lb_authvar_presence_endpoint presence_endpoint;
	struct payload_mm_authvar_presence_backing presence_backing;
	struct lb_authvar_presence_lifecycle_close_endpoint close_endpoint;
	uint64_t close_backing_base;
	uint64_t close_backing_bytes;
	uint32_t close_backing_tag;
	uint32_t close_backing_reserved;
	uint32_t presence_terminal;
	uint32_t lifecycle_close_state;
};

_Static_assert(sizeof(struct payload_mm_authvar_presence_s3_facts) == 208,
	"authenticated-variable S3 semantic facts layout");

size_t payload_mm_authvar_presence_s3_record_size(void);

/* Cold-only initialization.  The complete supplied region is cleared. */
enum cb_err payload_mm_authvar_presence_s3_record_cold_clear(
	void *storage, size_t storage_size);

/* Record an installed cold route after presence is terminally CLOSED. */
enum cb_err payload_mm_authvar_presence_s3_record_cold_activate(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts);

/* Seal the exact retained identity before entering S3. */
enum cb_err payload_mm_authvar_presence_s3_record_suspend_seal(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts);

/* Single-use S3 claim.  A valid, disjoint output is zeroed on failure. */
enum cb_err payload_mm_authvar_presence_s3_record_resume_borrow(
	void *storage, size_t storage_size,
	struct payload_mm_authvar_presence_s3_facts *facts);

/* Complete reconstruction of the same terminal semantic identity. */
enum cb_err payload_mm_authvar_presence_s3_record_rearm_commit(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts);

/* Any abandoned or ambiguous rearm is terminal for this boot. */
void payload_mm_authvar_presence_s3_record_poison(
	void *storage, size_t storage_size);

#if ENV_TEST
enum payload_mm_authvar_presence_s3_record_test_point {
	PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_COLD_ACTIVATE_CLAIMED = 1,
	PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_SUSPEND_CLAIMED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_BORROW_CLAIMED,
	PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_REARM_CLAIMED,
};

typedef void (*payload_mm_authvar_presence_s3_record_test_hook_fn)(
	enum payload_mm_authvar_presence_s3_record_test_point point);

uint32_t payload_mm_authvar_presence_s3_record_state_test(
	const void *storage, size_t storage_size);
void payload_mm_authvar_presence_s3_record_test_hook(
	payload_mm_authvar_presence_s3_record_test_hook_fn hook);
#endif

#endif
