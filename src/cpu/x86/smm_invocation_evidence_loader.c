/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_evidence.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_evidence_test_hook(uint32_t point);
#define EVIDENCE_LOADER_TEST_HOOK(point) smm_invocation_evidence_test_hook(point)
#else
#define EVIDENCE_LOADER_TEST_HOOK(point) do { } while (0)
#endif

#define INVOCATION_SHUTDOWN_REQUESTED (1U << 9)
#define STATE_PHASE_MASK 0x1fU

static void scrub_bytes(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool range_valid(const void *address, size_t size)
{
	const uintptr_t base = (uintptr_t)address;

	return address && size && base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!range_valid(first, first_size) || !range_valid(second, second_size))
		return true;
	return first_base <= second_base + second_size - 1U &&
		second_base <= first_base + first_size - 1U;
}

static bool seed_valid(const struct smm_invocation_loader_seed *seed)
{
	if (seed->revision != SMM_INVOCATION_EVIDENCE_REVISION ||
	    seed->size != sizeof(*seed) ||
	    (seed->lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD &&
	     seed->lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD) ||
	    seed->reserved ||
	    smm_invocation_loader_instance_nonce_is_zero(
		seed->loader_instance_nonce) || !seed->active_cpus ||
	    seed->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    seed->bsp_cpu >= seed->active_cpus)
		return false;
	for (uint32_t cpu = 0; cpu < seed->active_cpus; cpu++) {
		for (uint32_t other = cpu + 1U; other < seed->active_cpus; other++)
			if (seed->participant_apic_ids[cpu] ==
			    seed->participant_apic_ids[other])
				return false;
	}
	return true;
}

static bool terminal_claim(struct smm_invocation_evidence *evidence,
	uint32_t expected, uint32_t terminal)
{
	if (!__atomic_compare_exchange_n(&evidence->state, &expected,
		SMM_INVOCATION_TERMINAL_SCRUBBING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return false;
	scrub_bytes((uint8_t *)evidence + sizeof(evidence->state),
		sizeof(*evidence) - sizeof(evidence->state));
	__atomic_store_n(&evidence->state, terminal, __ATOMIC_RELEASE);
	return true;
}

static bool close_if_shutdown(struct smm_invocation_evidence *evidence,
	struct smm_invocation_evidence_loader_receipt *receipt)
{
	const uint32_t state = __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE);

	if ((state & STATE_PHASE_MASK) != SMM_INVOCATION_PROVISIONING ||
	    !(state & INVOCATION_SHUTDOWN_REQUESTED) ||
	    !terminal_claim(evidence, state, SMM_INVOCATION_CLOSED))
		return false;
	receipt->terminal_state = SMM_INVOCATION_CLOSED;
	return true;
}

static void finish_failure(struct smm_invocation_evidence *evidence,
	struct smm_invocation_evidence_loader_receipt *receipt)
{
	/* Only the monotonic shutdown latch can race this owned transition. */
	for (uint32_t attempt = 0; attempt < 2U; attempt++) {
		const uint32_t state = __atomic_load_n(&evidence->state,
			__ATOMIC_ACQUIRE);
		uint32_t terminal;

		if ((state & STATE_PHASE_MASK) != SMM_INVOCATION_PROVISIONING)
			return;
		terminal = state & INVOCATION_SHUTDOWN_REQUESTED ?
			SMM_INVOCATION_CLOSED : SMM_INVOCATION_POISONED;
		EVIDENCE_LOADER_TEST_HOOK(47);
		if (terminal_claim(evidence, state, terminal)) {
			receipt->terminal_state = terminal;
			return;
		}
	}
}

enum cb_err smm_invocation_evidence_loader_rollback(
	struct smm_invocation_evidence *evidence,
	struct smm_invocation_evidence_loader_receipt *receipt)
{
	uint32_t expected;
	enum cb_err result = CB_ERR;

	if (!range_valid(evidence, sizeof(*evidence)) ||
	    (uintptr_t)evidence % _Alignof(*evidence) ||
	    !range_valid(receipt, sizeof(*receipt)) ||
	    (uintptr_t)receipt % _Alignof(*receipt) ||
	    ranges_overlap(evidence, sizeof(*evidence), receipt,
		sizeof(*receipt)))
		return CB_ERR_ARG;
	if (receipt->evidence_identity != (uint64_t)(uintptr_t)evidence ||
	    receipt->acquired != 1U || receipt->reserved ||
	    (receipt->terminal_state != SMM_INVOCATION_READY &&
	     receipt->terminal_state != SMM_INVOCATION_CLOSED &&
	     receipt->terminal_state != SMM_INVOCATION_POISONED))
		goto out;
	expected = receipt->terminal_state;
	if (!terminal_claim(evidence, expected, SMM_INVOCATION_EMPTY))
		goto out;
	result = CB_SUCCESS;
out:
	scrub_bytes(receipt, sizeof(*receipt));
	return result;
}

enum cb_err smm_invocation_evidence_loader_provision(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_seed *seed,
	struct smm_invocation_evidence_loader_receipt *receipt)
{
	struct smm_invocation_loader_seed snapshot;
	uint32_t expected = SMM_INVOCATION_EMPTY;

	if (!range_valid(receipt, sizeof(*receipt)) ||
	    (uintptr_t)receipt % _Alignof(*receipt) ||
	    ranges_overlap(receipt, sizeof(*receipt), evidence,
		sizeof(*evidence)) ||
	    ranges_overlap(receipt, sizeof(*receipt), seed, sizeof(*seed)))
		return CB_ERR_ARG;
	scrub_bytes(receipt, sizeof(*receipt));
	if (!range_valid(evidence, sizeof(*evidence)) ||
	    (uintptr_t)evidence % _Alignof(*evidence) ||
	    !range_valid(seed, sizeof(*seed)) ||
	    (uintptr_t)seed % _Alignof(*seed) ||
	    ranges_overlap(evidence, sizeof(*evidence), seed, sizeof(*seed)))
		return CB_ERR;
	memcpy(&snapshot, seed, sizeof(snapshot));
	if (!seed_valid(&snapshot) ||
	    !__atomic_compare_exchange_n(&evidence->state, &expected,
		SMM_INVOCATION_PROVISIONING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		scrub_bytes(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	receipt->evidence_identity = (uint64_t)(uintptr_t)evidence;
	receipt->acquired = 1U;
	if (memcmp(seed, &snapshot, sizeof(snapshot)))
		goto fail;
	EVIDENCE_LOADER_TEST_HOOK(36);
	if (close_if_shutdown(evidence, receipt)) {
		EVIDENCE_LOADER_TEST_HOOK(48);
		scrub_bytes(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	EVIDENCE_LOADER_TEST_HOOK(37);
	if (__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_PROVISIONING ||
	    memcmp(seed, &snapshot, sizeof(snapshot)))
		goto fail;
	scrub_bytes((uint8_t *)evidence + sizeof(evidence->state),
		sizeof(*evidence) - sizeof(evidence->state));
	EVIDENCE_LOADER_TEST_HOOK(39);
	if (close_if_shutdown(evidence, receipt)) {
		EVIDENCE_LOADER_TEST_HOOK(49);
		scrub_bytes(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	EVIDENCE_LOADER_TEST_HOOK(38);
	evidence->active_cpus = snapshot.active_cpus;
	evidence->bsp_cpu = snapshot.bsp_cpu;
	evidence->loader_instance_nonce = snapshot.loader_instance_nonce;
	evidence->loader_lifecycle = snapshot.lifecycle;
	memcpy(evidence->participant_apic_ids, snapshot.participant_apic_ids,
		snapshot.active_cpus * sizeof(snapshot.participant_apic_ids[0]));
	evidence->expected_cpus = snapshot.active_cpus == 64U ? UINT64_MAX :
		(1ULL << snapshot.active_cpus) - 1ULL;
	EVIDENCE_LOADER_TEST_HOOK(44);
	if (close_if_shutdown(evidence, receipt)) {
		EVIDENCE_LOADER_TEST_HOOK(50);
		scrub_bytes(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	EVIDENCE_LOADER_TEST_HOOK(46);
	if (memcmp(seed, &snapshot, sizeof(snapshot)) ||
	    __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_PROVISIONING ||
	    evidence->active_cpus != snapshot.active_cpus ||
	    evidence->bsp_cpu != snapshot.bsp_cpu ||
	    !smm_invocation_loader_instance_nonce_equal(
		evidence->loader_instance_nonce,
		snapshot.loader_instance_nonce) ||
	    evidence->loader_lifecycle != snapshot.lifecycle ||
	    memcmp(evidence->participant_apic_ids,
		snapshot.participant_apic_ids,
		snapshot.active_cpus *
			sizeof(snapshot.participant_apic_ids[0])) ||
	    evidence->expected_cpus != (snapshot.active_cpus == 64U ? UINT64_MAX :
		(1ULL << snapshot.active_cpus) - 1ULL))
		goto fail;
	scrub_bytes(&snapshot, sizeof(snapshot));
	EVIDENCE_LOADER_TEST_HOOK(45);
	expected = SMM_INVOCATION_PROVISIONING;
	if (__atomic_compare_exchange_n(&evidence->state, &expected,
		SMM_INVOCATION_READY, false, __ATOMIC_RELEASE,
		__ATOMIC_ACQUIRE)) {
		receipt->terminal_state = SMM_INVOCATION_READY;
		return CB_SUCCESS;
	}
	if ((expected & STATE_PHASE_MASK) == SMM_INVOCATION_PROVISIONING &&
	    expected & INVOCATION_SHUTDOWN_REQUESTED &&
	    terminal_claim(evidence, expected, SMM_INVOCATION_CLOSED))
		receipt->terminal_state = SMM_INVOCATION_CLOSED;
	return CB_ERR;
fail:
	scrub_bytes(&snapshot, sizeof(snapshot));
	finish_failure(evidence, receipt);
	return CB_ERR;
}

enum cb_err smm_invocation_evidence_provision(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_seed *seed)
{
	struct smm_invocation_evidence_loader_receipt receipt;
	enum cb_err result;

	result = smm_invocation_evidence_loader_provision(evidence, seed,
		&receipt);
	scrub_bytes(&receipt, sizeof(receipt));
	return result == CB_SUCCESS ? CB_SUCCESS : CB_ERR;
}
