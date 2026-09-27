/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_evidence_test_hook(uint32_t point);
#define TEST_HOOK(point) smm_invocation_evidence_test_hook(point)
#else
#define TEST_HOOK(point) do { } while (0)
#endif

#define STATE_PHASE_MASK 0x1fU
#define ADMISSION_KIND_SHIFT 5
#define ADMISSION_KIND_MASK (0x3U << ADMISSION_KIND_SHIFT)
#define ADMISSION_BUSY (1U << 7)
#define ADMISSION_CONSUMED (1U << 8)
#define INVOCATION_SHUTDOWN_REQUESTED (1U << 9)
#define INVOCATION_REENTRY_DETECTED (1U << 10)
#define INVOCATION_CLOSE_REQUESTED (1U << 11)
#define INVOCATION_LATCH_MASK (INVOCATION_SHUTDOWN_REQUESTED | \
	INVOCATION_REENTRY_DETECTED | INVOCATION_CLOSE_REQUESTED)
#define INVOCATION_LATCH_COUNT 3U
#define INVOCATION_LATCH_CAS_ATTEMPTS (INVOCATION_LATCH_COUNT + 1U)
#define ADMISSION_NONCE_SHIFT 12
#define ADMISSION_NONCE_MAX (UINT32_MAX >> ADMISSION_NONCE_SHIFT)

_Static_assert(SMM_INVOCATION_POISONED <= STATE_PHASE_MASK,
	"invocation phases exceed packed state");
_Static_assert(SMM_INVOCATION_ADMISSION_ACK <= 0x3U,
	"admission kinds exceed packed state");

static uint32_t phase_load(const struct smm_invocation_evidence *evidence)
{
	return __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE) & STATE_PHASE_MASK;
}

uint32_t smm_invocation_evidence_phase(
	const struct smm_invocation_evidence *evidence)
{
	return evidence ? phase_load(evidence) : SMM_INVOCATION_POISONED;
}

bool smm_invocation_evidence_shutdown_requested(
	const struct smm_invocation_evidence *evidence)
{
	return evidence && (__atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE) & INVOCATION_SHUTDOWN_REQUESTED);
}

static bool shutdown_request(struct smm_invocation_evidence *evidence,
	uint32_t phase)
{
	uint32_t state = __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE);

	for (;;) {
		if ((state & STATE_PHASE_MASK) != phase)
			return false;
		if (state & INVOCATION_SHUTDOWN_REQUESTED)
			return true;
		if (__atomic_compare_exchange_n(&evidence->state, &state,
			state | INVOCATION_SHUTDOWN_REQUESTED, false,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return true;
	}
}

static bool close_requested(const struct smm_invocation_evidence *evidence)
{
	return __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) &
		INVOCATION_CLOSE_REQUESTED;
}

static void __noreturn invocation_fail_stop(void);

static bool phase_claim(struct smm_invocation_evidence *evidence,
	uint32_t from, uint32_t to)
{
	uint32_t state = __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE);

	if ((state & STATE_PHASE_MASK) != from)
		return false;
	if (from == SMM_INVOCATION_OPENING &&
	    to == SMM_INVOCATION_COLLECTING)
		TEST_HOOK(41);
	else if (from == SMM_INVOCATION_ACK_ARMING &&
		 to == SMM_INVOCATION_READY)
		TEST_HOOK(42);
	else if (from == SMM_INVOCATION_ACK_ADMITTING &&
		 to == SMM_INVOCATION_COLLECTING)
		TEST_HOOK(43);
	for (uint32_t attempt = 0;
	     attempt < INVOCATION_LATCH_CAS_ATTEMPTS; attempt++) {
		const uint32_t next = (state & ~STATE_PHASE_MASK) | to;

		if (__atomic_compare_exchange_n(&evidence->state, &state, next,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return true;
		if ((state & STATE_PHASE_MASK) != from)
			return false;
	}
	invocation_fail_stop();
}

static bool state_claim_exact(struct smm_invocation_evidence *evidence,
	uint32_t state, uint32_t phase)
{
	const uint32_t next = (state & ~STATE_PHASE_MASK) | phase;

	return __atomic_compare_exchange_n(&evidence->state, &state, next,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void phase_publish(struct smm_invocation_evidence *evidence,
	uint32_t phase)
{
	uint32_t state = __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE);

	for (;;) {
		const uint32_t next = (state & ~STATE_PHASE_MASK) | phase;

		if (__atomic_compare_exchange_n(&evidence->state,
			&state, next, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
			break;
	}
}

static void scrub(void *address, size_t size)
{
	volatile uint8_t *byte = address;

	while (size--)
		*byte++ = 0;
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

static uint64_t mix64(uint64_t value)
{
	value ^= value >> 30;
	value *= 0xbf58476d1ce4e5b9ULL;
	value ^= value >> 27;
	value *= 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

static struct smm_invocation_loader_instance_nonce nonce_load(
	const struct smm_invocation_loader_instance_nonce *nonce)
{
	return (struct smm_invocation_loader_instance_nonce) {
		.low = __atomic_load_n(&nonce->low, __ATOMIC_ACQUIRE),
		.high = __atomic_load_n(&nonce->high, __ATOMIC_ACQUIRE),
	};
}

static void nonce_store(struct smm_invocation_loader_instance_nonce *target,
	struct smm_invocation_loader_instance_nonce nonce)
{
	__atomic_store_n(&target->low, nonce.low, __ATOMIC_RELAXED);
	__atomic_store_n(&target->high, nonce.high, __ATOMIC_RELAXED);
}

static void invocation_scrub(struct smm_invocation_evidence *evidence)
{
	for (uint32_t cpu = 0; cpu < SMM_INVOCATION_EVIDENCE_MAX_CPUS; cpu++)
		scrub(&evidence->participants[cpu],
			sizeof(evidence->participants[cpu]));
	__atomic_store_n(&evidence->arrived_cpus, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&evidence->rendezvous_ack_cpus, 0U,
		__ATOMIC_RELAXED);
	__atomic_store_n(&evidence->departed_cpus, 0U, __ATOMIC_RELAXED);
	evidence->sentinel = 0;
	evidence->original_rax = 0;
	evidence->command = 0;
	evidence->command_reserved = 0;
	scrub(&evidence->token, sizeof(evidence->token));
	evidence->close_reserved = 0;
	evidence->reentry_reserved = 0;
	__atomic_store_n(&evidence->arrival_failed, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&evidence->departure_failed, 0U, __ATOMIC_RELAXED);
}

static void scrub_fields(struct smm_invocation_evidence *evidence)
{
	invocation_scrub(evidence);
	/* Keep stale failure requesters from writing a closed object. */
	__atomic_store_n(&evidence->rendezvous_fail_requested, 1U,
		__ATOMIC_RELAXED);
	evidence->active_cpus = 0;
	evidence->bsp_cpu = 0;
	nonce_store(&evidence->loader_instance_nonce,
		(struct smm_invocation_loader_instance_nonce) { 0 });
	evidence->closed_generation = 0;
	evidence->closed_loader_instance_nonce =
		(struct smm_invocation_loader_instance_nonce) { 0 };
	__atomic_store_n(&evidence->loader_lifecycle, 0U, __ATOMIC_RELAXED);
	evidence->closed_lifecycle = 0;
	__atomic_store_n(&evidence->closed_eos_consumed, 0U,
		__ATOMIC_RELAXED);
	evidence->close_receipt_reserved = 0;
	evidence->expected_cpus = 0;
	__atomic_store_n(&evidence->generation, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&evidence->arrival_writers, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&evidence->departure_writers, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&evidence->rendezvous_ack_required, 0U,
		__ATOMIC_RELAXED);
	evidence->shutdown_reserved = 0;
	evidence->admission_reserved = 0;
	scrub(evidence->participant_apic_ids,
		sizeof(evidence->participant_apic_ids));
}

static void terminal_scrub(struct smm_invocation_evidence *evidence)
{
	uint32_t state = __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE);

	for (;;) {
		const uint32_t terminal = (state & ~STATE_PHASE_MASK) |
			SMM_INVOCATION_TERMINAL_SCRUBBING;

		if (__atomic_compare_exchange_n(&evidence->state, &state,
			terminal, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			break;
	}
	TEST_HOOK(38);
	scrub_fields(evidence);
	__atomic_store_n(&evidence->state, SMM_INVOCATION_TERMINAL_SCRUBBING,
		__ATOMIC_RELAXED);
}

static bool admission_reserve(struct smm_invocation_evidence *evidence,
	uint32_t kind, uint32_t expected_phase, uint32_t owned_phase)
{
	uint32_t control = __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE);
	uint32_t nonce;
	uint32_t reserved;

	if ((control & STATE_PHASE_MASK) != expected_phase ||
	    control & (ADMISSION_BUSY | INVOCATION_LATCH_MASK))
		return false;
	nonce = (control >> ADMISSION_NONCE_SHIFT) + 1U;
	if (!nonce || nonce > ADMISSION_NONCE_MAX)
		return false;
	reserved = (control & INVOCATION_LATCH_MASK) |
		(nonce << ADMISSION_NONCE_SHIFT) |
		(kind << ADMISSION_KIND_SHIFT) | ADMISSION_BUSY | owned_phase;
	return __atomic_compare_exchange_n(&evidence->state,
		&control, reserved, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static bool admission_complete(struct smm_invocation_evidence *evidence,
	uint32_t kind, uint32_t *completed_control)
{
	uint32_t control;

	/* Return the observed control with completion status. */
	for (uint32_t attempt = 0; attempt < 4U; attempt++) {
		control = __atomic_load_n(&evidence->state,
			__ATOMIC_ACQUIRE);
		if ((control & (ADMISSION_KIND_MASK | ADMISSION_BUSY)) !=
		    ((kind << ADMISSION_KIND_SHIFT) | ADMISSION_BUSY)) {
			if (completed_control)
				*completed_control = control;
			return false;
		}
		if (!attempt)
			TEST_HOOK(35);
		if (__atomic_compare_exchange_n(&evidence->state,
			&control, control & ~ADMISSION_BUSY, false,
			__ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
			if (completed_control)
				*completed_control = control;
			return true;
		}
	}
	invocation_fail_stop();
}

static bool invocation_progress_phase(uint32_t phase)
{
	return phase == SMM_INVOCATION_COLLECTING ||
		(phase >= SMM_INVOCATION_CLAIMING &&
		 phase <= SMM_INVOCATION_CLOSING);
}

static void admission_token_fill_control(
	struct smm_invocation_evidence *evidence,
	struct smm_invocation_admission_token *token, uint32_t kind,
	uint64_t invocation_generation, uint32_t control)
{
	*token = (struct smm_invocation_admission_token) {
		.evidence_identity = (uintptr_t)evidence,
		.attempt_nonce = control >> ADMISSION_NONCE_SHIFT,
		.loader_instance_nonce = nonce_load(&evidence->loader_instance_nonce),
		.invocation_generation = invocation_generation,
		.lifecycle = __atomic_load_n(&evidence->loader_lifecycle,
			__ATOMIC_ACQUIRE),
		.kind = (control & ADMISSION_KIND_MASK) >> ADMISSION_KIND_SHIFT,
	};
	if (!token->kind)
		token->kind = kind;
}

static void admission_token_fill(struct smm_invocation_evidence *evidence,
	struct smm_invocation_admission_token *token, uint32_t kind,
	uint64_t invocation_generation)
{
	admission_token_fill_control(evidence, token, kind,
		invocation_generation,
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE));
}

static bool admission_retry_token(struct smm_invocation_evidence *evidence,
	struct smm_invocation_admission_token *token, uint32_t kind,
	uint64_t invocation_generation, uint32_t phase)
{
	const uint32_t control = __atomic_load_n(&evidence->state,
		__ATOMIC_ACQUIRE);

	if ((control & (ADMISSION_KIND_MASK | ADMISSION_BUSY)) !=
	    ((kind << ADMISSION_KIND_SHIFT) | ADMISSION_BUSY) ||
	    !(control >> ADMISSION_NONCE_SHIFT))
		return false;
	admission_token_fill_control(evidence, token, kind,
		invocation_generation, control);
	return phase_load(evidence) == phase &&
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) ==
			control;
}

static bool cleanup_claim(struct smm_invocation_evidence *evidence,
	uint32_t from, uint32_t to, uint32_t hook)
{
	uint32_t original = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
	uint32_t state = original;

	(void)hook;
	if ((state & STATE_PHASE_MASK) != from)
		return false;
	TEST_HOOK(hook);
	for (uint32_t attempt = 0;
	     attempt < INVOCATION_LATCH_CAS_ATTEMPTS; attempt++) {
		const uint32_t next = (state & ~STATE_PHASE_MASK) | to;

		if (__atomic_compare_exchange_n(&evidence->state, &state, next,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return true;
		if ((state & STATE_PHASE_MASK) != from ||
		    ((state ^ original) & ~INVOCATION_LATCH_MASK) ||
		    (state & original & INVOCATION_LATCH_MASK) !=
			(original & INVOCATION_LATCH_MASK))
			return false;
	}
	invocation_fail_stop();
}

static void poison_finish_if_quiescent(
	struct smm_invocation_evidence *evidence)
{
	if (__atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->departure_writers, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) &
		ADMISSION_BUSY)
		return;
	if (!cleanup_claim(evidence, SMM_INVOCATION_POISONING,
		SMM_INVOCATION_POISON_CLEANING, 39))
		return;
	terminal_scrub(evidence);
	phase_publish(evidence, SMM_INVOCATION_POISONED);
}

static enum cb_err poison_owned(struct smm_invocation_evidence *evidence,
	uint32_t owned_phase)
{
	uint32_t state;

	/* Shutdown and reentry are one-way packed latches in this owned phase. */
	for (uint32_t attempt = 0; attempt < 3U; attempt++) {
		state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
		if ((state & STATE_PHASE_MASK) != owned_phase) {
			poison_finish_if_quiescent(evidence);
			return CB_ERR;
		}
		if (state_claim_exact(evidence, state,
			SMM_INVOCATION_POISONING))
			goto claimed;
	}
	invocation_fail_stop();

claimed:
	poison_finish_if_quiescent(evidence);
	return CB_ERR;
}

static bool poison_admit_state(struct smm_invocation_evidence *evidence,
	uint32_t state, uint32_t owned_phase)
{
	if ((state & STATE_PHASE_MASK) != owned_phase ||
	    !state_claim_exact(evidence, state,
		SMM_INVOCATION_POISON_ADMITTING))
		return false;
	TEST_HOOK(23);
	__atomic_store_n(&evidence->rendezvous_fail_requested, 1U,
		__ATOMIC_RELEASE);
	__atomic_store_n(&evidence->arrival_failed, 1U, __ATOMIC_RELEASE);
	phase_publish(evidence, SMM_INVOCATION_POISONING);
	poison_finish_if_quiescent(evidence);
	return true;
}

static bool poison_admit(struct smm_invocation_evidence *evidence,
	uint32_t owned_phase)
{
	return poison_admit_state(evidence,
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE), owned_phase);
}

static void rendezvous_poison_requested(
	struct smm_invocation_evidence *evidence)
{
	if (!__atomic_load_n(&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE) ||
	    !poison_admit(evidence, SMM_INVOCATION_COLLECTING))
		return;
}

static void __noreturn invocation_fail_stop(void)
{
	smm_invocation_platform_fail_stop();
}

static enum cb_err invocation_open_owned(
	struct smm_invocation_evidence *evidence)
{
	invocation_scrub(evidence);
	evidence->closed_generation = 0;
	evidence->closed_loader_instance_nonce =
		(struct smm_invocation_loader_instance_nonce) { 0 };
	evidence->closed_lifecycle = 0;
	__atomic_store_n(&evidence->closed_eos_consumed, 0U,
		__ATOMIC_RELAXED);
	if (smm_invocation_evidence_shutdown_requested(evidence) ||
	    evidence->generation == UINT64_MAX)
		return poison_owned(evidence, SMM_INVOCATION_OPENING);
	evidence->generation++;
	if (!evidence->generation)
		return poison_owned(evidence, SMM_INVOCATION_OPENING);
	if (!phase_claim(evidence, SMM_INVOCATION_OPENING,
		SMM_INVOCATION_COLLECTING)) {
		poison_finish_if_quiescent(evidence);
		return CB_ERR;
	}
	return CB_SUCCESS;
}

enum smm_invocation_try_result smm_invocation_evidence_arrive_try(
	struct smm_invocation_evidence *evidence, uint32_t cpu, uint32_t apic_id,
	uint64_t *generation, struct smm_invocation_admission_token *token)
{
	struct smm_invocation_participant *participant;
	uint32_t empty = SMM_INVOCATION_PARTICIPANT_EMPTY;
	uint32_t completed_control;
	uint32_t phase;
	bool admission_completed;
	bool opening_owner = false;
	bool rendezvous_failed;
	bool resampled = false;
	uint32_t state;

	if (!evidence || !generation || !token ||
	    !range_valid(generation, sizeof(*generation)) ||
	    ranges_overlap(evidence, sizeof(*evidence), generation,
		sizeof(*generation)))
		return SMM_INVOCATION_TRY_ERROR;
retry_phase:
	state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
	if (state & INVOCATION_LATCH_MASK)
		return SMM_INVOCATION_TRY_ERROR;
	admission_token_fill(evidence, token, SMM_INVOCATION_ADMISSION_ARRIVE,
		__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE));
	TEST_HOOK(25);
	phase = phase_load(evidence);
	if (phase == SMM_INVOCATION_READY) {
		TEST_HOOK(27);
		if (!admission_reserve(evidence,
			SMM_INVOCATION_ADMISSION_ARRIVE,
			SMM_INVOCATION_READY, SMM_INVOCATION_OPENING)) {
			if (admission_retry_token(evidence, token,
				SMM_INVOCATION_ADMISSION_ARRIVE,
				__atomic_load_n(&evidence->generation,
					__ATOMIC_ACQUIRE), phase))
				return SMM_INVOCATION_TRY_RETRY;
			if (!resampled) {
				resampled = true;
				goto retry_phase;
			}
			(void)poison_owned(evidence, SMM_INVOCATION_READY);
			return SMM_INVOCATION_TRY_ERROR;
		}
		admission_token_fill(evidence, token,
			SMM_INVOCATION_ADMISSION_ARRIVE,
			__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE));
		TEST_HOOK(17);
		opening_owner = true;
		TEST_HOOK(1);
	} else {
		if (phase == SMM_INVOCATION_OPENING ||
		    phase == SMM_INVOCATION_ARRIVAL_ADMITTING) {
			TEST_HOOK(20);
			if (admission_retry_token(evidence, token,
				SMM_INVOCATION_ADMISSION_ARRIVE,
				__atomic_load_n(&evidence->generation,
					__ATOMIC_ACQUIRE), phase))
				return SMM_INVOCATION_TRY_RETRY;
			if (!resampled) {
				resampled = true;
				goto retry_phase;
			}
			return SMM_INVOCATION_TRY_ERROR;
		}
		if (phase != SMM_INVOCATION_COLLECTING)
			return SMM_INVOCATION_TRY_ERROR;
		TEST_HOOK(27);
		if (!admission_reserve(evidence,
			SMM_INVOCATION_ADMISSION_ARRIVE,
			SMM_INVOCATION_COLLECTING,
			SMM_INVOCATION_ARRIVAL_ADMITTING)) {
			if (admission_retry_token(evidence, token,
				SMM_INVOCATION_ADMISSION_ARRIVE,
				__atomic_load_n(&evidence->generation,
					__ATOMIC_ACQUIRE), phase))
				return SMM_INVOCATION_TRY_RETRY;
			if (!resampled) {
				resampled = true;
				goto retry_phase;
			}
			(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);
			return SMM_INVOCATION_TRY_ERROR;
		}
		admission_token_fill(evidence, token,
			SMM_INVOCATION_ADMISSION_ARRIVE,
			__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE));
		TEST_HOOK(5);
	}
	__atomic_fetch_add(&evidence->arrival_writers, 1U, __ATOMIC_ACQ_REL);
	if (opening_owner && invocation_open_owned(evidence) != CB_SUCCESS) {
		__atomic_store_n(&evidence->arrival_failed, 1U, __ATOMIC_RELEASE);
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ARRIVE, NULL);
		__atomic_fetch_sub(&evidence->arrival_writers, 1U,
			__ATOMIC_RELEASE);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	if (!opening_owner &&
	    !phase_claim(evidence, SMM_INVOCATION_ARRIVAL_ADMITTING,
		SMM_INVOCATION_COLLECTING)) {
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ARRIVE, NULL);
		poison_finish_if_quiescent(evidence);
		__atomic_fetch_sub(&evidence->arrival_writers, 1U,
			__ATOMIC_RELEASE);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	phase = phase_load(evidence);
	if (phase != SMM_INVOCATION_COLLECTING ||
	    cpu >= evidence->active_cpus ||
	    apic_id != evidence->participant_apic_ids[cpu]) {
		__atomic_store_n(&evidence->arrival_failed, 1U, __ATOMIC_RELEASE);
		(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ARRIVE, NULL);
		__atomic_fetch_sub(&evidence->arrival_writers, 1U,
			__ATOMIC_RELEASE);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}

	participant = &evidence->participants[cpu];
	if (!__atomic_compare_exchange_n(&participant->phase, &empty,
		SMM_INVOCATION_PARTICIPANT_WRITING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		__atomic_store_n(&evidence->arrival_failed, 1U, __ATOMIC_RELEASE);
		__atomic_fetch_sub(&evidence->arrival_writers, 1U,
			__ATOMIC_ACQ_REL);
		(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ARRIVE, NULL);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	participant->apic_id = apic_id;
	participant->generation = evidence->generation;
	__atomic_store_n(&participant->phase, SMM_INVOCATION_PARTICIPANT_READY,
		__ATOMIC_RELEASE);
	__atomic_fetch_or(&evidence->arrived_cpus, 1ULL << cpu,
		__ATOMIC_ACQ_REL);
	*generation = evidence->generation;
	TEST_HOOK(7);
	__atomic_fetch_sub(&evidence->arrival_writers, 1U, __ATOMIC_RELEASE);
	rendezvous_failed = __atomic_load_n(
		&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE);
	if (rendezvous_failed)
		rendezvous_poison_requested(evidence);
	poison_finish_if_quiescent(evidence);
	TEST_HOOK(8);
	if (rendezvous_failed ||
	    __atomic_load_n(&evidence->arrival_failed, __ATOMIC_ACQUIRE) ||
	    smm_invocation_evidence_shutdown_requested(evidence)) {
		*generation = 0;
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ARRIVE, NULL);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	admission_completed = admission_complete(evidence,
		SMM_INVOCATION_ADMISSION_ARRIVE, &completed_control);
	if (!admission_completed ||
	    completed_control & (ADMISSION_CONSUMED |
		INVOCATION_SHUTDOWN_REQUESTED) ||
	    !invocation_progress_phase(completed_control & STATE_PHASE_MASK)) {
		*generation = 0;
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	return SMM_INVOCATION_TRY_SUCCESS;
}

bool smm_invocation_evidence_rendezvous_ready(
	const struct smm_invocation_evidence *evidence, uint64_t generation)
{
	if (!evidence || !generation ||
	    phase_load(evidence) != SMM_INVOCATION_COLLECTING)
		return false;
	if (__atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->arrival_failed, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE) ||
	    smm_invocation_evidence_shutdown_requested(evidence))
		return false;

	return __atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE) ==
		generation &&
		__atomic_load_n(&evidence->arrived_cpus, __ATOMIC_ACQUIRE) ==
		evidence->expected_cpus;
}

enum smm_invocation_try_result
smm_invocation_evidence_require_rendezvous_ack_try(
	struct smm_invocation_evidence *evidence,
	struct smm_invocation_loader_instance_nonce loader_instance_nonce,
	uint32_t lifecycle, struct smm_invocation_admission_token *token)
{
	uint32_t completed_control;
	uint32_t expected;
	uint32_t phase;
	uint32_t state;
	bool admission_completed;
	bool resampled = false;

	if (!evidence || !token ||
	    smm_invocation_loader_instance_nonce_is_zero(loader_instance_nonce) ||
	    (lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD &&
	     lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD))
		return SMM_INVOCATION_TRY_ERROR;
retry_phase:
	state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
	if (state & INVOCATION_LATCH_MASK)
		return SMM_INVOCATION_TRY_ERROR;
	admission_token_fill(evidence, token, SMM_INVOCATION_ADMISSION_ARM, 0);
	TEST_HOOK(25);
	token->loader_instance_nonce = loader_instance_nonce;
	token->lifecycle = lifecycle;
	expected = __atomic_load_n(&evidence->rendezvous_ack_required,
		__ATOMIC_ACQUIRE);
	if (expected == 1U) {
		phase = phase_load(evidence);
		if (phase == SMM_INVOCATION_ACK_ARMING) {
			if (!smm_invocation_loader_instance_nonce_equal(
				nonce_load(&evidence->loader_instance_nonce),
				loader_instance_nonce) ||
			    __atomic_load_n(&evidence->loader_lifecycle,
				__ATOMIC_ACQUIRE) != lifecycle)
				return SMM_INVOCATION_TRY_ERROR;
			if (admission_retry_token(evidence, token,
				SMM_INVOCATION_ADMISSION_ARM, 0, phase))
				return SMM_INVOCATION_TRY_RETRY;
			if (!resampled) {
				resampled = true;
				goto retry_phase;
			}
			return SMM_INVOCATION_TRY_ERROR;
		}
		if (phase != SMM_INVOCATION_READY &&
		    phase != SMM_INVOCATION_OPENING &&
		    phase != SMM_INVOCATION_ARRIVAL_ADMITTING &&
		    phase != SMM_INVOCATION_COLLECTING)
			return SMM_INVOCATION_TRY_ERROR;
		return smm_invocation_loader_instance_nonce_equal(
				nonce_load(&evidence->loader_instance_nonce),
				loader_instance_nonce) &&
			__atomic_load_n(&evidence->loader_lifecycle,
				__ATOMIC_ACQUIRE) == lifecycle ?
			SMM_INVOCATION_TRY_SUCCESS : SMM_INVOCATION_TRY_ERROR;
	}
	if (expected)
		return SMM_INVOCATION_TRY_ERROR;
	phase = phase_load(evidence);
	if (phase == SMM_INVOCATION_ACK_ARMING) {
		TEST_HOOK(19);
		if (admission_retry_token(evidence, token,
			SMM_INVOCATION_ADMISSION_ARM, 0, phase))
			return SMM_INVOCATION_TRY_RETRY;
		if (!resampled) {
			resampled = true;
			goto retry_phase;
		}
		return SMM_INVOCATION_TRY_ERROR;
	}
	if (phase != SMM_INVOCATION_READY)
		return SMM_INVOCATION_TRY_ERROR;
	TEST_HOOK(27);
	if (!admission_reserve(evidence, SMM_INVOCATION_ADMISSION_ARM,
		SMM_INVOCATION_READY, SMM_INVOCATION_ACK_ARMING)) {
		if (admission_retry_token(evidence, token,
			SMM_INVOCATION_ADMISSION_ARM, 0, phase))
			return SMM_INVOCATION_TRY_RETRY;
		if (!resampled) {
			resampled = true;
			goto retry_phase;
		}
		(void)poison_owned(evidence, SMM_INVOCATION_READY);
		return SMM_INVOCATION_TRY_ERROR;
	}
	admission_token_fill(evidence, token, SMM_INVOCATION_ADMISSION_ARM, 0);
	TEST_HOOK(16);
	TEST_HOOK(9);
	if (smm_invocation_evidence_shutdown_requested(evidence)) {
		if (!phase_claim(evidence, SMM_INVOCATION_ACK_ARMING,
			SMM_INVOCATION_CLOSING)) {
			(void)admission_complete(evidence,
				SMM_INVOCATION_ADMISSION_ARM, NULL);
			poison_finish_if_quiescent(evidence);
			return SMM_INVOCATION_TRY_ERROR;
		}
		terminal_scrub(evidence);
		phase_publish(evidence, SMM_INVOCATION_CLOSED);
		return SMM_INVOCATION_TRY_ERROR;
	}
	if (__atomic_load_n(&evidence->rendezvous_fail_requested,
			__ATOMIC_ACQUIRE) ||
	    !smm_invocation_loader_instance_nonce_equal(
		nonce_load(&evidence->loader_instance_nonce),
		loader_instance_nonce) ||
	    __atomic_load_n(&evidence->loader_lifecycle,
			__ATOMIC_ACQUIRE) != lifecycle) {
		if (__atomic_load_n(&evidence->rendezvous_fail_requested,
			__ATOMIC_ACQUIRE))
			(void)poison_owned(evidence, SMM_INVOCATION_ACK_ARMING);
		else if (!phase_claim(evidence, SMM_INVOCATION_ACK_ARMING,
			SMM_INVOCATION_READY))
			poison_finish_if_quiescent(evidence);
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ARM, NULL);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	__atomic_store_n(&evidence->rendezvous_ack_required, 1U,
		__ATOMIC_RELEASE);
	TEST_HOOK(44);
	if (!phase_claim(evidence, SMM_INVOCATION_ACK_ARMING,
		SMM_INVOCATION_READY)) {
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ARM, NULL);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	admission_completed = admission_complete(evidence,
		SMM_INVOCATION_ADMISSION_ARM, &completed_control);
	if (!admission_completed ||
	    completed_control & (ADMISSION_CONSUMED |
		INVOCATION_SHUTDOWN_REQUESTED) ||
	    (completed_control & STATE_PHASE_MASK) != SMM_INVOCATION_READY)
		return SMM_INVOCATION_TRY_ERROR;
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum cb_err smm_invocation_evidence_admission_fail(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_admission_token *token)
{
	uint32_t phase;
	uint32_t control;
	uint32_t consumed;
	uint32_t target;
	bool generation_matches;

	if (!evidence || !token ||
	    token->evidence_identity != (uintptr_t)evidence ||
	    !token->attempt_nonce || token->token_reserved ||
	    smm_invocation_loader_instance_nonce_is_zero(
		token->loader_instance_nonce) ||
	    token->lifecycle != __atomic_load_n(&evidence->loader_lifecycle,
		__ATOMIC_ACQUIRE) ||
	    !smm_invocation_loader_instance_nonce_equal(
		token->loader_instance_nonce,
		nonce_load(&evidence->loader_instance_nonce)))
		return CB_ERR;
	if (token->kind != SMM_INVOCATION_ADMISSION_ARM &&
	    token->kind != SMM_INVOCATION_ADMISSION_ARRIVE &&
	    token->kind != SMM_INVOCATION_ADMISSION_ACK)
		return CB_ERR;
	generation_matches = token->invocation_generation ==
		__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE);
	if (token->kind == SMM_INVOCATION_ADMISSION_ARRIVE &&
	    token->invocation_generation != UINT64_MAX)
		generation_matches |= token->invocation_generation + 1U ==
			__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE);
	if ((token->kind == SMM_INVOCATION_ADMISSION_ACK &&
	     !token->invocation_generation) ||
	    (token->kind != SMM_INVOCATION_ADMISSION_ARM &&
	     !generation_matches))
		return CB_ERR;
	for (uint32_t attempt = 0; attempt < 2U; attempt++) {
		control = __atomic_load_n(&evidence->state,
			__ATOMIC_ACQUIRE);
		if (((control & ADMISSION_KIND_MASK) >> ADMISSION_KIND_SHIFT) !=
		    token->kind ||
		    control & ADMISSION_CONSUMED ||
		    control >> ADMISSION_NONCE_SHIFT != token->attempt_nonce)
			return CB_ERR;
		phase = control & STATE_PHASE_MASK;
		if ((token->kind == SMM_INVOCATION_ADMISSION_ARM &&
		     phase == SMM_INVOCATION_ACK_ARMING) ||
		    (token->kind == SMM_INVOCATION_ADMISSION_ARRIVE &&
		     (phase == SMM_INVOCATION_OPENING ||
		      phase == SMM_INVOCATION_ARRIVAL_ADMITTING)) ||
		    (token->kind == SMM_INVOCATION_ADMISSION_ACK &&
		     phase == SMM_INVOCATION_ACK_ADMITTING))
			target = SMM_INVOCATION_ADMISSION_FAILED;
		else if ((phase == SMM_INVOCATION_READY &&
			  ((control & ADMISSION_BUSY) ||
			   (token->kind == SMM_INVOCATION_ADMISSION_ARM &&
			    __atomic_load_n(
				&evidence->rendezvous_ack_required,
				__ATOMIC_ACQUIRE)))) ||
			 phase == SMM_INVOCATION_COLLECTING)
			target = SMM_INVOCATION_POISON_ADMITTING;
		else
			return CB_ERR;
		if (!attempt)
			TEST_HOOK(22);
		consumed = (control & ~STATE_PHASE_MASK) | target |
			ADMISSION_CONSUMED;
		if (__atomic_compare_exchange_n(&evidence->state,
			&control, consumed, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			goto claimed;
	}
	return CB_ERR;

claimed:
	TEST_HOOK(24);
	__atomic_store_n(&evidence->rendezvous_fail_requested, 1U,
		__ATOMIC_RELEASE);
	if (!shutdown_request(evidence, target))
		invocation_fail_stop();
	phase_publish(evidence, SMM_INVOCATION_POISONING);
	poison_finish_if_quiescent(evidence);
	return CB_ERR;
}

enum cb_err smm_invocation_evidence_rendezvous_fail(
	struct smm_invocation_evidence *evidence, uint64_t generation)
{
	uint32_t state;

	if (!evidence || !generation)
		return CB_ERR;
	state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
	if ((state & STATE_PHASE_MASK) != SMM_INVOCATION_COLLECTING ||
	    __atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE) !=
		generation)
		return CB_ERR;
	TEST_HOOK(11);
	if (__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE) !=
		generation ||
	    !poison_admit_state(evidence, state,
		SMM_INVOCATION_COLLECTING))
		return CB_ERR;
	return CB_ERR;
}

enum smm_invocation_try_result smm_invocation_evidence_rendezvous_ack_try(
	struct smm_invocation_evidence *evidence, uint64_t generation,
	uint32_t cpu, struct smm_invocation_admission_token *token)
{
	uint64_t old;
	uint32_t completed_control;
	uint32_t phase;
	uint32_t state;
	bool admission_completed;
	bool resampled = false;

	if (!evidence || !generation || !token)
		return SMM_INVOCATION_TRY_ERROR;
retry_phase:
	state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
	if (state & INVOCATION_LATCH_MASK)
		return SMM_INVOCATION_TRY_ERROR;
	admission_token_fill(evidence, token, SMM_INVOCATION_ADMISSION_ACK,
		generation);
	TEST_HOOK(25);
	phase = phase_load(evidence);
	if (phase == SMM_INVOCATION_ACK_ADMITTING) {
		TEST_HOOK(21);
		if (admission_retry_token(evidence, token,
			SMM_INVOCATION_ADMISSION_ACK, generation, phase))
			return SMM_INVOCATION_TRY_RETRY;
		if (!resampled) {
			resampled = true;
			goto retry_phase;
		}
		return SMM_INVOCATION_TRY_ERROR;
	}
	if (phase != SMM_INVOCATION_COLLECTING)
		return SMM_INVOCATION_TRY_ERROR;
	TEST_HOOK(27);
	if (!admission_reserve(evidence, SMM_INVOCATION_ADMISSION_ACK,
		SMM_INVOCATION_COLLECTING, SMM_INVOCATION_ACK_ADMITTING)) {
		if (admission_retry_token(evidence, token,
			SMM_INVOCATION_ADMISSION_ACK, generation, phase))
			return SMM_INVOCATION_TRY_RETRY;
		if (!resampled) {
			resampled = true;
			goto retry_phase;
		}
		(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);
		return SMM_INVOCATION_TRY_ERROR;
	}
	admission_token_fill(evidence, token, SMM_INVOCATION_ADMISSION_ACK,
		generation);
	TEST_HOOK(18);
	if (cpu >= evidence->active_cpus ||
	    __atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE) !=
		generation ||
	    !__atomic_load_n(&evidence->rendezvous_ack_required,
		__ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->arrival_failed, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE) ||
	    smm_invocation_evidence_shutdown_requested(evidence) ||
	    __atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->arrived_cpus, __ATOMIC_ACQUIRE) !=
		evidence->expected_cpus) {
		if (!phase_claim(evidence, SMM_INVOCATION_ACK_ADMITTING,
			SMM_INVOCATION_COLLECTING))
			poison_finish_if_quiescent(evidence);
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ACK, NULL);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	TEST_HOOK(10);
	if (__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE) !=
		generation ||
	    __atomic_load_n(&evidence->arrival_failed, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE) ||
	    smm_invocation_evidence_shutdown_requested(evidence)) {
		if (!phase_claim(evidence, SMM_INVOCATION_ACK_ADMITTING,
			SMM_INVOCATION_COLLECTING))
			poison_finish_if_quiescent(evidence);
		rendezvous_poison_requested(evidence);
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ACK, NULL);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	old = __atomic_fetch_or(&evidence->rendezvous_ack_cpus, 1ULL << cpu,
		__ATOMIC_ACQ_REL);
	if (!phase_claim(evidence, SMM_INVOCATION_ACK_ADMITTING,
		SMM_INVOCATION_COLLECTING)) {
		(void)admission_complete(evidence,
			SMM_INVOCATION_ADMISSION_ACK, NULL);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	admission_completed = admission_complete(evidence,
		SMM_INVOCATION_ADMISSION_ACK, &completed_control);
	if (!admission_completed ||
	    completed_control & (ADMISSION_CONSUMED |
		INVOCATION_SHUTDOWN_REQUESTED) ||
	    !invocation_progress_phase(completed_control & STATE_PHASE_MASK))
		return SMM_INVOCATION_TRY_ERROR;
	if (old & (1ULL << cpu))
		return SMM_INVOCATION_TRY_ERROR;
	return SMM_INVOCATION_TRY_SUCCESS;
}

bool smm_invocation_evidence_rendezvous_ack_ready(
	const struct smm_invocation_evidence *evidence, uint64_t generation)
{
	uint32_t phase;

	if (!evidence || !generation)
		return false;
	phase = phase_load(evidence);
	return (phase == SMM_INVOCATION_COLLECTING ||
		(phase >= SMM_INVOCATION_CLAIMING &&
		 phase <= SMM_INVOCATION_CLOSING)) &&
		__atomic_load_n(&evidence->generation, __ATOMIC_ACQUIRE) ==
			generation &&
		__atomic_load_n(&evidence->rendezvous_ack_required,
			__ATOMIC_ACQUIRE) &&
		!__atomic_load_n(&evidence->rendezvous_fail_requested,
			__ATOMIC_ACQUIRE) &&
		__atomic_load_n(&evidence->rendezvous_ack_cpus,
			__ATOMIC_ACQUIRE) == evidence->expected_cpus;
}

static bool ops_valid(const struct smm_invocation_save_state_ops *ops)
{
	return ops && ops->match_apmc_write && ops->read_rax && ops->write_rax &&
		(!ops->context_size ||
		 range_valid(ops->context, ops->context_size));
}

static bool ops_unchanged(const struct smm_invocation_save_state_ops *ops,
	const struct smm_invocation_save_state_ops *snapshot)
{
	return !memcmp(ops, snapshot, sizeof(*snapshot));
}

static bool callback_reentered(struct smm_invocation_evidence *evidence)
{
	return __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) &
		INVOCATION_REENTRY_DETECTED;
}

static void record_reentry(struct smm_invocation_evidence *evidence,
	uint32_t phase)
{
	uint32_t state;

	if (phase == SMM_INVOCATION_CLAIMING ||
	    phase == SMM_INVOCATION_PUBLISHING ||
	    phase == SMM_INVOCATION_ABORTING) {
		state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
		if ((state & STATE_PHASE_MASK) != phase ||
		    state & INVOCATION_REENTRY_DETECTED)
			return;
		TEST_HOOK(34);
		(void)__atomic_compare_exchange_n(&evidence->state, &state,
			state | INVOCATION_REENTRY_DETECTED, false,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	}
}

static bool operation_ranges_valid(struct smm_invocation_evidence *evidence,
	const struct smm_invocation_save_state_ops *ops, const void *object,
	size_t object_size)
{
	if (!range_valid(evidence, sizeof(*evidence)) ||
	    !range_valid(ops, sizeof(*ops)) || !range_valid(object, object_size) ||
	    ranges_overlap(evidence, sizeof(*evidence), ops, sizeof(*ops)) ||
	    ranges_overlap(evidence, sizeof(*evidence), object, object_size) ||
	    ranges_overlap(ops, sizeof(*ops), object, object_size))
		return false;
	if (!ops->context_size)
		return true;
	return !ranges_overlap(evidence, sizeof(*evidence), ops->context,
			ops->context_size) &&
		!ranges_overlap(ops, sizeof(*ops), ops->context,
			ops->context_size) &&
		!ranges_overlap(object, object_size, ops->context,
			ops->context_size);
}

static bool restore_or_fail_stop(struct smm_invocation_evidence *evidence,
	const struct smm_invocation_save_state_ops *source,
	const struct smm_invocation_save_state_ops *ops, uint32_t initiator)
{
	uint64_t readback = 0;
	bool unchanged;

	if (ops->write_rax(ops->context, initiator, evidence->original_rax) !=
		CB_SUCCESS)
		invocation_fail_stop();
	unchanged = ops_unchanged(source, ops) && !callback_reentered(evidence);
	if (ops->read_rax(ops->context, initiator, &readback) != CB_SUCCESS)
		invocation_fail_stop();
	unchanged = unchanged && ops_unchanged(source, ops) &&
		!callback_reentered(evidence);
	if (readback != evidence->original_rax)
		invocation_fail_stop();
	return unchanged;
}

static enum cb_err close_owned(struct smm_invocation_evidence *evidence,
	uint32_t owned_phase)
{
	uint32_t state;

	for (uint32_t attempt = 0; attempt < 3U; attempt++) {
		state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
		if ((state & STATE_PHASE_MASK) != owned_phase)
			return CB_ERR;
		if (state & INVOCATION_REENTRY_DETECTED)
			return poison_owned(evidence, owned_phase);
		if (__atomic_compare_exchange_n(&evidence->state, &state,
			(state & ~STATE_PHASE_MASK) |
				INVOCATION_CLOSE_REQUESTED |
				SMM_INVOCATION_CLOSING,
			false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			return CB_ERR;
	}
	invocation_fail_stop();
	return CB_ERR;
}

static void build_token(struct smm_invocation_evidence *evidence,
	uint32_t initiator, uint8_t command, uint64_t sentinel,
	struct smm_invocation_token *token)
{
	uint64_t proof[3] = {
		evidence->loader_instance_nonce.low ^ sentinel,
		evidence->generation ^ command,
		evidence->loader_instance_nonce.high ^
			((uint64_t)evidence->active_cpus << 32) ^
			evidence->bsp_cpu ^ initiator,
	};

	for (uint32_t cpu = 0; cpu < evidence->active_cpus; cpu++) {
		const uint64_t participant = ((uint64_t)cpu << 32) |
			evidence->participant_apic_ids[cpu];

		proof[0] = mix64(proof[0] ^ participant);
		proof[1] = mix64(proof[1] ^ participant ^ proof[0]);
		proof[2] = mix64(proof[2] ^ participant ^ proof[1]);
	}

	*token = (struct smm_invocation_token) {
		.revision = SMM_INVOCATION_TOKEN_REVISION,
		.size = sizeof(*token),
		.initiator_cpu = initiator,
		.active_cpus = evidence->active_cpus,
		.smi_generation = evidence->generation,
		.rendezvous_generation = evidence->generation,
		.rendezvous_digest = {
			mix64(proof[0] ^ evidence->loader_instance_nonce.low),
			mix64(proof[1] ^ sentinel),
			mix64(proof[2] ^ command ^
				evidence->loader_instance_nonce.high),
		},
		.bsp = 1,
	};
}

enum cb_err smm_invocation_evidence_claim(
	struct smm_invocation_evidence *evidence, uint8_t command,
	uint64_t sentinel, const struct smm_invocation_save_state_ops *ops,
	struct smm_invocation_token *token)
{
	struct smm_invocation_save_state_ops snapshot;
	struct smm_invocation_token local_token;
	uint64_t readback;
	uint32_t initiator = UINT32_MAX;
	uint32_t matches = 0;

	if (!evidence || !token || !ops_valid(ops) || !sentinel ||
	    (uint8_t)sentinel != command ||
	    !operation_ranges_valid(evidence, ops, token, sizeof(*token)))
		return CB_ERR;
	record_reentry(evidence, phase_load(evidence));
	memcpy(&snapshot, ops, sizeof(snapshot));
	if (smm_invocation_evidence_shutdown_requested(evidence) ||
	    __atomic_load_n(&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->arrived_cpus, __ATOMIC_ACQUIRE) !=
	    evidence->expected_cpus ||
	    (__atomic_load_n(&evidence->rendezvous_ack_required,
		__ATOMIC_ACQUIRE) &&
	     __atomic_load_n(&evidence->rendezvous_ack_cpus,
		__ATOMIC_ACQUIRE) != evidence->expected_cpus) ||
	    !phase_claim(evidence, SMM_INVOCATION_COLLECTING,
		SMM_INVOCATION_CLAIMING))
		return CB_ERR;
	if (smm_invocation_evidence_shutdown_requested(evidence) ||
	    __atomic_load_n(&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE)) {
		if (__atomic_load_n(&evidence->rendezvous_fail_requested,
			__ATOMIC_ACQUIRE))
			(void)poison_owned(evidence, SMM_INVOCATION_CLAIMING);
		else
			(void)close_owned(evidence, SMM_INVOCATION_CLAIMING);
		return CB_ERR;
	}

	for (uint32_t cpu = 0; cpu < evidence->active_cpus; cpu++) {
		const struct smm_invocation_participant *participant =
			&evidence->participants[cpu];
		enum smm_invocation_match match;

		if (__atomic_load_n(&participant->phase, __ATOMIC_ACQUIRE) !=
			SMM_INVOCATION_PARTICIPANT_READY ||
		    participant->generation != evidence->generation ||
		    participant->apic_id != evidence->participant_apic_ids[cpu])
			return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
		match = snapshot.match_apmc_write(snapshot.context, cpu, command);
		if (callback_reentered(evidence) ||
		    !ops_unchanged(ops, &snapshot) ||
		    (match != SMM_INVOCATION_NOT_MATCHED &&
		     match != SMM_INVOCATION_MATCHED))
			return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
		if (smm_invocation_evidence_shutdown_requested(evidence))
			return close_owned(evidence, SMM_INVOCATION_CLAIMING);
		if (match == SMM_INVOCATION_MATCHED) {
			matches++;
			initiator = cpu;
		}
	}
	if (matches != 1U || initiator != evidence->bsp_cpu)
		return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
	if (snapshot.read_rax(snapshot.context, initiator,
		&evidence->original_rax) != CB_SUCCESS ||
	    callback_reentered(evidence) || !ops_unchanged(ops, &snapshot) ||
	    (uint8_t)evidence->original_rax != command)
		return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
	if (smm_invocation_evidence_shutdown_requested(evidence))
		return close_owned(evidence, SMM_INVOCATION_CLAIMING);
	if (snapshot.write_rax(snapshot.context, initiator, sentinel) !=
		CB_SUCCESS) {
		(void)restore_or_fail_stop(evidence, ops, &snapshot, initiator);
		return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
	}
	if (callback_reentered(evidence) || !ops_unchanged(ops, &snapshot) ||
	    snapshot.read_rax(snapshot.context, initiator, &readback) !=
		CB_SUCCESS || !ops_unchanged(ops, &snapshot) || readback != sentinel) {
		(void)restore_or_fail_stop(evidence, ops, &snapshot, initiator);
		return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
	}
	if (smm_invocation_evidence_shutdown_requested(evidence)) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot, initiator))
			return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
		(void)close_owned(evidence, SMM_INVOCATION_CLAIMING);
		return CB_ERR;
	}

	build_token(evidence, initiator, command, sentinel, &local_token);
	evidence->sentinel = sentinel;
	evidence->command = command;
	evidence->token = local_token;
	TEST_HOOK(2);
	if (callback_reentered(evidence) ||
	    smm_invocation_evidence_shutdown_requested(evidence) ||
	    __atomic_load_n(&evidence->rendezvous_fail_requested,
		__ATOMIC_ACQUIRE)) {
		(void)restore_or_fail_stop(evidence, ops, &snapshot, initiator);
		return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
	}
	if (!phase_claim(evidence, SMM_INVOCATION_CLAIMING,
		SMM_INVOCATION_CLAIMED)) {
		(void)restore_or_fail_stop(evidence, ops, &snapshot, initiator);
		invocation_fail_stop();
	}
	if (smm_invocation_evidence_shutdown_requested(evidence)) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot, initiator))
			return poison_owned(evidence, SMM_INVOCATION_CLAIMED);
		(void)close_owned(evidence, SMM_INVOCATION_CLAIMED);
		scrub(&snapshot, sizeof(snapshot));
		scrub(&local_token, sizeof(local_token));
		return CB_ERR;
	}
	*token = local_token;
	scrub(&snapshot, sizeof(snapshot));
	scrub(&local_token, sizeof(local_token));
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_publish(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_token *token, uint64_t value,
	const struct smm_invocation_save_state_ops *ops)
{
	struct smm_invocation_save_state_ops snapshot;
	struct smm_invocation_token token_snapshot;
	uint64_t readback;

	if (!evidence || !token || !ops_valid(ops) ||
	    !operation_ranges_valid(evidence, ops, token, sizeof(*token)))
		return CB_ERR;
	record_reentry(evidence, phase_load(evidence));
	memcpy(&token_snapshot, token, sizeof(token_snapshot));
	memcpy(&snapshot, ops, sizeof(snapshot));
	if (memcmp(&token_snapshot, &evidence->token, sizeof(token_snapshot)))
		return CB_ERR;
	if (!phase_claim(evidence, SMM_INVOCATION_CLAIMED,
		SMM_INVOCATION_PUBLISHING))
		return CB_ERR;
	if (memcmp(&token_snapshot, &evidence->token, sizeof(token_snapshot)))
		return poison_owned(evidence, SMM_INVOCATION_PUBLISHING);
	if (value == evidence->sentinel) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot,
			token_snapshot.initiator_cpu))
			return poison_owned(evidence,
				SMM_INVOCATION_PUBLISHING);
		return close_owned(evidence, SMM_INVOCATION_PUBLISHING);
	}
	if (smm_invocation_evidence_shutdown_requested(evidence)) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot,
			token_snapshot.initiator_cpu))
			return poison_owned(evidence,
				SMM_INVOCATION_PUBLISHING);
		(void)close_owned(evidence, SMM_INVOCATION_PUBLISHING);
		return CB_ERR;
	}
	if (snapshot.read_rax(snapshot.context, token_snapshot.initiator_cpu,
		&readback) !=
		CB_SUCCESS || callback_reentered(evidence) ||
	    !ops_unchanged(ops, &snapshot) ||
	    readback != evidence->sentinel) {
		(void)restore_or_fail_stop(evidence, ops, &snapshot,
			token_snapshot.initiator_cpu);
		scrub(&snapshot, sizeof(snapshot));
		return poison_owned(evidence, SMM_INVOCATION_PUBLISHING);
	}
	if (smm_invocation_evidence_shutdown_requested(evidence)) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot,
			token_snapshot.initiator_cpu))
			return poison_owned(evidence,
				SMM_INVOCATION_PUBLISHING);
		return close_owned(evidence, SMM_INVOCATION_PUBLISHING);
	}
	if (snapshot.write_rax(snapshot.context, token_snapshot.initiator_cpu,
		value) != CB_SUCCESS || !ops_unchanged(ops, &snapshot) ||
	    snapshot.read_rax(snapshot.context, token_snapshot.initiator_cpu,
		&readback) != CB_SUCCESS || !ops_unchanged(ops, &snapshot) ||
	    callback_reentered(evidence) || readback != value)
		invocation_fail_stop();
	TEST_HOOK(4);
	if (!phase_claim(evidence, SMM_INVOCATION_PUBLISHING,
		SMM_INVOCATION_PUBLISHED))
		invocation_fail_stop();
	if (smm_invocation_evidence_shutdown_requested(evidence)) {
		if (!phase_claim(evidence, SMM_INVOCATION_PUBLISHED,
			SMM_INVOCATION_COMPLETING))
			invocation_fail_stop();
		(void)close_owned(evidence, SMM_INVOCATION_COMPLETING);
		scrub(&snapshot, sizeof(snapshot));
		scrub(&token_snapshot, sizeof(token_snapshot));
		return CB_ERR;
	}
	scrub(&snapshot, sizeof(snapshot));
	scrub(&token_snapshot, sizeof(token_snapshot));
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_complete(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_token *token)
{
	struct smm_invocation_token snapshot;

	if (!evidence || !range_valid(token, sizeof(*token)) ||
	    ranges_overlap(evidence, sizeof(*evidence), token, sizeof(*token)))
		return CB_ERR;
	memcpy(&snapshot, token, sizeof(snapshot));
	if (!phase_claim(evidence, SMM_INVOCATION_PUBLISHED,
		SMM_INVOCATION_COMPLETING))
		return CB_ERR;
	if (memcmp(&snapshot, &evidence->token, sizeof(snapshot)))
		return poison_owned(evidence, SMM_INVOCATION_COMPLETING);
	(void)close_owned(evidence, SMM_INVOCATION_COMPLETING);
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_abort(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_token *token,
	const struct smm_invocation_save_state_ops *ops)
{
	struct smm_invocation_save_state_ops ops_snapshot;
	struct smm_invocation_token token_snapshot;

	if (!evidence || !token || !ops_valid(ops) ||
	    !operation_ranges_valid(evidence, ops, token, sizeof(*token)))
		return CB_ERR;
	record_reentry(evidence, phase_load(evidence));
	memcpy(&token_snapshot, token, sizeof(token_snapshot));
	memcpy(&ops_snapshot, ops, sizeof(ops_snapshot));
	if (!phase_claim(evidence, SMM_INVOCATION_CLAIMED,
		SMM_INVOCATION_ABORTING))
		return CB_ERR;
	if (memcmp(&token_snapshot, &evidence->token,
		sizeof(token_snapshot)) || !ops_unchanged(ops, &ops_snapshot))
		return poison_owned(evidence, SMM_INVOCATION_ABORTING);
	if (!restore_or_fail_stop(evidence, ops, &ops_snapshot,
		token_snapshot.initiator_cpu))
		return poison_owned(evidence, SMM_INVOCATION_ABORTING);
	(void)close_owned(evidence, SMM_INVOCATION_ABORTING);
	scrub(&ops_snapshot, sizeof(ops_snapshot));
	scrub(&token_snapshot, sizeof(token_snapshot));
	return CB_SUCCESS;
}

static void close_finish_if_quiescent(
	struct smm_invocation_evidence *evidence)
{
	uint64_t closed_generation;
	struct smm_invocation_loader_instance_nonce
		closed_loader_instance_nonce;
	uint32_t closed_lifecycle;
	uint32_t state;

	if (!cleanup_claim(evidence, SMM_INVOCATION_CLOSE_CLEANING,
		SMM_INVOCATION_CLOSE_SCRUBBING, 40))
		return;

	closed_generation = evidence->generation;
	closed_loader_instance_nonce = evidence->loader_instance_nonce;
	closed_lifecycle = evidence->loader_lifecycle;
	invocation_scrub(evidence);
	__atomic_store_n(&evidence->rendezvous_ack_required, 0U,
		__ATOMIC_RELAXED);
	if (smm_invocation_evidence_shutdown_requested(evidence)) {
		terminal_scrub(evidence);
		phase_publish(evidence, SMM_INVOCATION_CLOSED);
	} else {
		evidence->closed_generation = closed_generation;
		evidence->closed_loader_instance_nonce = closed_loader_instance_nonce;
		evidence->closed_lifecycle = closed_lifecycle;
		__atomic_store_n(&evidence->closed_eos_consumed, 0U,
			__ATOMIC_RELAXED);
		state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
		for (;;) {
			const uint32_t ready =
				(state & ~(STATE_PHASE_MASK |
					INVOCATION_CLOSE_REQUESTED)) |
				SMM_INVOCATION_READY;

			if (__atomic_compare_exchange_n(&evidence->state, &state,
				ready, false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
				break;
		}
	}
}

enum smm_invocation_try_result smm_invocation_evidence_depart_try(
	struct smm_invocation_evidence *evidence, uint32_t cpu,
	uint64_t generation)
{
	uint64_t old;
	uint32_t phase;
	uint32_t state;

	if (!evidence)
		return SMM_INVOCATION_TRY_ERROR;
	state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
	phase = state & STATE_PHASE_MASK;
	if (phase == SMM_INVOCATION_DEPARTURE_ADMITTING ||
	    phase == SMM_INVOCATION_DEPARTURE_COMMITTING)
		return SMM_INVOCATION_TRY_RETRY;
	if (phase != SMM_INVOCATION_CLOSING ||
	    generation != __atomic_load_n(&evidence->generation,
		__ATOMIC_ACQUIRE))
		return SMM_INVOCATION_TRY_ERROR;
	TEST_HOOK(32);
	if (!state_claim_exact(evidence, state,
		SMM_INVOCATION_DEPARTURE_ADMITTING))
		return SMM_INVOCATION_TRY_RETRY;
	TEST_HOOK(6);
	__atomic_fetch_add(&evidence->departure_writers, 1U, __ATOMIC_ACQ_REL);
	TEST_HOOK(3);
	phase = phase_load(evidence);
	if (phase != SMM_INVOCATION_DEPARTURE_ADMITTING ||
	    cpu >= evidence->active_cpus ||
	    generation != evidence->generation ||
	    __atomic_load_n(&evidence->departure_failed, __ATOMIC_ACQUIRE)) {
		(void)phase_claim(evidence, SMM_INVOCATION_DEPARTURE_ADMITTING,
			SMM_INVOCATION_CLOSING);
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	TEST_HOOK(28);
	if (!phase_claim(evidence, SMM_INVOCATION_DEPARTURE_ADMITTING,
		SMM_INVOCATION_DEPARTURE_COMMITTING)) {
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	TEST_HOOK(29);
	old = __atomic_fetch_or(&evidence->departed_cpus,
		1ULL << cpu, __ATOMIC_ACQ_REL);
	if (old & (1ULL << cpu)) {
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		(void)poison_owned(evidence,
			SMM_INVOCATION_DEPARTURE_COMMITTING);
		poison_finish_if_quiescent(evidence);
		return SMM_INVOCATION_TRY_ERROR;
	}
	if ((old | (1ULL << cpu)) != evidence->expected_cpus) {
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		phase_publish(evidence, SMM_INVOCATION_CLOSING);
		TEST_HOOK(54);
		return SMM_INVOCATION_TRY_SUCCESS;
	}
	if (!close_requested(evidence) ||
	    !phase_claim(evidence, SMM_INVOCATION_DEPARTURE_COMMITTING,
		SMM_INVOCATION_CLOSE_CLEANING)) {
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		(void)poison_owned(evidence,
			SMM_INVOCATION_DEPARTURE_COMMITTING);
		return SMM_INVOCATION_TRY_ERROR;
	}
	__atomic_fetch_sub(&evidence->departure_writers, 1U, __ATOMIC_RELEASE);
	close_finish_if_quiescent(evidence);
	return SMM_INVOCATION_TRY_SUCCESS;
}

enum cb_err smm_invocation_evidence_ticket_fail(
	struct smm_invocation_evidence *evidence, uint64_t generation)
{
	uint32_t phase;
	uint32_t state;

	if (!evidence || !generation)
		return CB_ERR;
	for (;;) {
		phase = phase_load(evidence);
		if (phase == SMM_INVOCATION_DEPARTURE_ADMITTING ||
		    phase == SMM_INVOCATION_CLOSING) {
			state = __atomic_load_n(&evidence->state,
				__ATOMIC_ACQUIRE);
			phase = state & STATE_PHASE_MASK;
			if (phase != SMM_INVOCATION_DEPARTURE_ADMITTING &&
			    phase != SMM_INVOCATION_CLOSING)
				continue;
			if (generation != __atomic_load_n(&evidence->generation,
				__ATOMIC_ACQUIRE))
				return CB_ERR;
			TEST_HOOK(31);
			if (!state_claim_exact(evidence, state,
				SMM_INVOCATION_DEPARTURE_FAIL_ADMITTING))
				return CB_ERR;
			__atomic_store_n(&evidence->departure_failed, 1U,
				__ATOMIC_RELEASE);
			TEST_HOOK(26);
			if (!shutdown_request(evidence,
				SMM_INVOCATION_DEPARTURE_FAIL_ADMITTING))
				invocation_fail_stop();
			phase_publish(evidence, SMM_INVOCATION_POISONING);
			poison_finish_if_quiescent(evidence);
			return CB_ERR;
		}
		if (phase != SMM_INVOCATION_READY ||
		    generation != evidence->closed_generation)
			return CB_ERR;
		state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
		if ((state & STATE_PHASE_MASK) != SMM_INVOCATION_READY ||
		    generation != evidence->closed_generation ||
		    !state_claim_exact(evidence, state,
			SMM_INVOCATION_CLOSING))
			return CB_ERR;
		if (!shutdown_request(evidence, SMM_INVOCATION_CLOSING))
			invocation_fail_stop();
		terminal_scrub(evidence);
		phase_publish(evidence, SMM_INVOCATION_CLOSED);
		return CB_ERR;
	}
}

bool smm_invocation_evidence_eos_consume(
	struct smm_invocation_evidence *evidence, uint64_t generation,
	struct smm_invocation_loader_instance_nonce loader_instance_nonce,
	uint32_t lifecycle, uint32_t cpu)
{
	uint32_t expected = 0;
	uint32_t state;

	if (!evidence || !generation ||
	    smm_invocation_loader_instance_nonce_is_zero(loader_instance_nonce))
		return false;
	state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
	if ((state & STATE_PHASE_MASK) != SMM_INVOCATION_READY ||
	    state & INVOCATION_SHUTDOWN_REQUESTED ||
	    cpu != evidence->bsp_cpu ||
	    generation != evidence->closed_generation ||
	    !smm_invocation_loader_instance_nonce_equal(loader_instance_nonce,
		evidence->closed_loader_instance_nonce) ||
	    lifecycle != evidence->closed_lifecycle ||
	    __atomic_load_n(&evidence->closed_eos_consumed,
		__ATOMIC_ACQUIRE))
		return false;
	TEST_HOOK(33);
	if (!state_claim_exact(evidence, state,
		SMM_INVOCATION_EOS_ADMITTING))
		return false;
	TEST_HOOK(12);
	if (smm_invocation_evidence_shutdown_requested(evidence) ||
	    cpu != evidence->bsp_cpu ||
	    generation != evidence->closed_generation ||
	    !smm_invocation_loader_instance_nonce_equal(loader_instance_nonce,
		evidence->closed_loader_instance_nonce) ||
	    lifecycle != evidence->closed_lifecycle ||
	    !__atomic_compare_exchange_n(&evidence->closed_eos_consumed,
		&expected, 1U, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		phase_publish(evidence, SMM_INVOCATION_READY);
		return false;
	}
	phase_publish(evidence, SMM_INVOCATION_READY);
	return true;
}

enum cb_err smm_invocation_evidence_shutdown(
	struct smm_invocation_evidence *evidence)
{
	uint32_t phase;
	uint32_t spins = 0;

	if (!evidence)
		return CB_ERR;
	for (;;) {
		phase = phase_load(evidence);
		TEST_HOOK(30);
		if (phase > SMM_INVOCATION_POISONED)
			invocation_fail_stop();
		if (phase == SMM_INVOCATION_CLOSED)
			return CB_SUCCESS;
		if (phase == SMM_INVOCATION_POISONED)
			return CB_ERR;
		if (phase == SMM_INVOCATION_TERMINAL_SCRUBBING) {
			if (++spins == 10000000U)
				invocation_fail_stop();
			__asm__ volatile ("pause");
			continue;
		}
		if (phase == SMM_INVOCATION_EMPTY) {
			if (!phase_claim(evidence, phase, SMM_INVOCATION_CLOSING))
				continue;
			terminal_scrub(evidence);
			phase_publish(evidence, SMM_INVOCATION_CLOSED);
			return CB_SUCCESS;
		}
		if (phase == SMM_INVOCATION_READY) {
			if (!phase_claim(evidence, phase, SMM_INVOCATION_CLOSING))
				continue;
			if (__atomic_load_n(&evidence->arrival_writers,
				__ATOMIC_ACQUIRE))
				invocation_fail_stop();
			terminal_scrub(evidence);
			phase_publish(evidence, SMM_INVOCATION_CLOSED);
			return CB_SUCCESS;
		}
		if (!shutdown_request(evidence, phase))
			continue;
		TEST_HOOK(37);
		if (phase == SMM_INVOCATION_CLAIMING ||
		    phase == SMM_INVOCATION_PUBLISHING ||
		    phase == SMM_INVOCATION_ABORTING) {
			record_reentry(evidence, phase);
			return CB_ERR;
		}
		if (phase == SMM_INVOCATION_CLAIMED)
			invocation_fail_stop();
		if (phase == SMM_INVOCATION_COLLECTING &&
		    !__atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) &&
		    __atomic_load_n(&evidence->arrived_cpus, __ATOMIC_ACQUIRE) ==
			evidence->expected_cpus) {
			(void)close_owned(evidence, phase);
			continue;
		}
		if (phase == SMM_INVOCATION_PUBLISHED &&
		    phase_claim(evidence, phase, SMM_INVOCATION_COMPLETING)) {
			(void)close_owned(evidence, SMM_INVOCATION_COMPLETING);
			continue;
		}
		if (++spins == 10000000U)
			invocation_fail_stop();
		__asm__ volatile ("pause");
	}
}
