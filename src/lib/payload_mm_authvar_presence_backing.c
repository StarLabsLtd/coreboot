/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_backing.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "Authenticated-variable presence backing evidence is SMM-only"
#endif

enum evidence_phase {
	EVIDENCE_EMPTY,
	EVIDENCE_PUBLISHING,
	EVIDENCE_PUBLISH_CLOSE_REQUESTED,
	EVIDENCE_READY,
	EVIDENCE_CLOSING,
	EVIDENCE_TAKING,
	EVIDENCE_TAKE_CLOSE_REQUESTED,
	EVIDENCE_CONSUMED,
	EVIDENCE_TERMINAL,
};

static struct {
	struct payload_mm_authvar_presence_backing backing;
	struct payload_mm_authvar_presence_backing sealed;
	uint32_t phase;
} evidence;
#if ENV_TEST
static payload_mm_authvar_presence_backing_test_hook_fn close_test_hook;
#endif

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

enum cb_err payload_mm_authvar_presence_backing_evidence_publish(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt,
	const struct payload_mm_authvar_presence_backing *backing)
{
	struct payload_mm_authvar_presence_backing backing_snapshot;
	struct bootmem_reservation_receipt receipt_snapshot;
	uint32_t expected;
	bool valid;

	if (!verifier || !receipt || !backing)
		return CB_ERR;
	backing_snapshot = *backing;
	receipt_snapshot = *receipt;
	if (backing_snapshot.revision !=
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION ||
	    backing_snapshot.size != sizeof(backing_snapshot) ||
	    !backing_snapshot.base ||
	    backing_snapshot.bytes != PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE ||
	    !backing_snapshot.generation ||
	    backing_snapshot.tag != BM_MEM_RESERVED ||
	    backing_snapshot.reserved ||
	    receipt_snapshot.base != backing_snapshot.base ||
	    receipt_snapshot.bytes != backing_snapshot.bytes ||
	    receipt_snapshot.generation != backing_snapshot.generation)
		return CB_ERR;
	expected = EVIDENCE_EMPTY;
	if (!__atomic_compare_exchange_n(&evidence.phase, &expected,
		EVIDENCE_PUBLISHING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	valid = bootmem_reservation_receipt_verify_consume_exact_tag(verifier,
		receipt, BM_MEM_RESERVED) == CB_SUCCESS &&
		zero(receipt, sizeof(*receipt)) &&
		!memcmp(backing, &backing_snapshot, sizeof(backing_snapshot));
	if (!valid)
		goto fail;
	evidence.backing = backing_snapshot;
	evidence.sealed = backing_snapshot;
	expected = EVIDENCE_PUBLISHING;
	if (__atomic_compare_exchange_n(&evidence.phase, &expected,
		EVIDENCE_READY, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		scrub(&backing_snapshot, sizeof(backing_snapshot));
		scrub(&receipt_snapshot, sizeof(receipt_snapshot));
		return CB_SUCCESS;
	}
fail:
	scrub(&evidence.backing, sizeof(evidence.backing));
	scrub(&evidence.sealed, sizeof(evidence.sealed));
	expected = EVIDENCE_PUBLISHING;
	if (!__atomic_compare_exchange_n(&evidence.phase, &expected,
		EVIDENCE_TERMINAL, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		expected = EVIDENCE_PUBLISH_CLOSE_REQUESTED;
		(void)__atomic_compare_exchange_n(&evidence.phase, &expected,
			EVIDENCE_TERMINAL, false, __ATOMIC_RELEASE,
			__ATOMIC_ACQUIRE);
	}
	scrub(&backing_snapshot, sizeof(backing_snapshot));
	scrub(&receipt_snapshot, sizeof(receipt_snapshot));
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_backing_evidence_take(
	const struct payload_mm_authvar_presence_backing *backing)
{
	struct payload_mm_authvar_presence_backing snapshot;
	uint32_t expected = EVIDENCE_READY;
	bool valid;

	if (!backing || !__atomic_compare_exchange_n(&evidence.phase, &expected,
		EVIDENCE_TAKING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	snapshot = evidence.sealed;
	valid = !memcmp(backing, &snapshot, sizeof(snapshot)) &&
		!memcmp(&evidence.backing, &snapshot, sizeof(snapshot)) &&
		!memcmp(&evidence.sealed, &snapshot, sizeof(snapshot));
	scrub(&evidence.backing, sizeof(evidence.backing));
	scrub(&evidence.sealed, sizeof(evidence.sealed));
	scrub(&snapshot, sizeof(snapshot));
	expected = EVIDENCE_TAKING;
	if (__atomic_compare_exchange_n(&evidence.phase, &expected,
		valid ? EVIDENCE_CONSUMED : EVIDENCE_TERMINAL, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		return valid ? CB_SUCCESS : CB_ERR;
	expected = EVIDENCE_TAKE_CLOSE_REQUESTED;
	(void)__atomic_compare_exchange_n(&evidence.phase, &expected,
		EVIDENCE_TERMINAL, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE);
	return CB_ERR;
}

void payload_mm_authvar_presence_backing_evidence_close(void)
{
	uint32_t phase;
	uint32_t expected;

	for (;;) {
		phase = __atomic_load_n(&evidence.phase, __ATOMIC_ACQUIRE);
		if (phase == EVIDENCE_TERMINAL)
			return;
		if (phase == EVIDENCE_PUBLISHING || phase == EVIDENCE_TAKING) {
			expected = phase;
			if (__atomic_compare_exchange_n(&evidence.phase, &expected,
				phase == EVIDENCE_PUBLISHING ?
					EVIDENCE_PUBLISH_CLOSE_REQUESTED :
					EVIDENCE_TAKE_CLOSE_REQUESTED,
				false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
				return;
			continue;
		}
		if (phase == EVIDENCE_PUBLISH_CLOSE_REQUESTED ||
		    phase == EVIDENCE_TAKE_CLOSE_REQUESTED ||
		    phase == EVIDENCE_CLOSING)
			return;
		expected = phase;
		if (phase == EVIDENCE_READY || phase == EVIDENCE_CONSUMED) {
			if (!__atomic_compare_exchange_n(&evidence.phase, &expected,
				EVIDENCE_CLOSING, false, __ATOMIC_ACQ_REL,
				__ATOMIC_ACQUIRE))
				continue;
#if ENV_TEST
			if (close_test_hook)
				close_test_hook();
#endif
			scrub(&evidence.backing, sizeof(evidence.backing));
			scrub(&evidence.sealed, sizeof(evidence.sealed));
			__atomic_store_n(&evidence.phase, EVIDENCE_TERMINAL,
				__ATOMIC_RELEASE);
			return;
		}
		if (!__atomic_compare_exchange_n(&evidence.phase, &expected,
			EVIDENCE_TERMINAL, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			continue;
		scrub(&evidence.backing, sizeof(evidence.backing));
		scrub(&evidence.sealed, sizeof(evidence.sealed));
		return;
	}
}

bool payload_mm_authvar_presence_backing_evidence_consumed(void)
{
	return __atomic_load_n(&evidence.phase, __ATOMIC_ACQUIRE) ==
		EVIDENCE_CONSUMED && zero(&evidence.backing, sizeof(evidence.backing)) &&
		zero(&evidence.sealed, sizeof(evidence.sealed));
}

#if ENV_TEST
void payload_mm_authvar_presence_backing_evidence_reset_test(void)
{
	scrub(&evidence, sizeof(evidence));
	close_test_hook = NULL;
}

void payload_mm_authvar_presence_backing_close_test_hook(
	payload_mm_authvar_presence_backing_test_hook_fn hook)
{
	close_test_hook = hook;
}

bool payload_mm_authvar_presence_backing_terminal_test(void)
{
	return __atomic_load_n(&evidence.phase, __ATOMIC_ACQUIRE) ==
		EVIDENCE_TERMINAL;
}

bool payload_mm_authvar_presence_backing_scrubbed_test(void)
{
	return zero(&evidence.backing, sizeof(evidence.backing)) &&
		zero(&evidence.sealed, sizeof(evidence.sealed));
}

#endif
