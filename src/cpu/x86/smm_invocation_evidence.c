/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_evidence.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_evidence_test_hook(uint32_t point);
#define TEST_HOOK(point) smm_invocation_evidence_test_hook(point)
#else
#define TEST_HOOK(point) do { } while (0)
#endif

static uint32_t phase_load(const struct smm_invocation_evidence *evidence)
{
	return __atomic_load_n(&evidence->phase, __ATOMIC_ACQUIRE);
}

static bool phase_claim(struct smm_invocation_evidence *evidence,
	uint32_t from, uint32_t to)
{
	return __atomic_compare_exchange_n(&evidence->phase, &from, to, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static void phase_publish(struct smm_invocation_evidence *evidence,
	uint32_t phase)
{
	__atomic_store_n(&evidence->phase, phase, __ATOMIC_RELEASE);
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

static uint64_t cpu_mask(uint32_t active_cpus)
{
	return active_cpus == 64U ? UINT64_MAX : (1ULL << active_cpus) - 1ULL;
}

static bool seed_valid(const struct smm_invocation_loader_seed *seed)
{
	if (seed->revision != SMM_INVOCATION_EVIDENCE_REVISION ||
	    seed->size != sizeof(*seed) ||
	    (seed->lifecycle != SMM_INVOCATION_LOADER_COLD &&
	     seed->lifecycle != SMM_INVOCATION_LOADER_RESUME_FRESH) ||
	    seed->reserved ||
	    !seed->boot_generation || !seed->active_cpus ||
	    seed->active_cpus > SMM_INVOCATION_EVIDENCE_MAX_CPUS ||
	    seed->bsp_cpu >= seed->active_cpus)
		return false;

	for (uint32_t cpu = 0; cpu < seed->active_cpus; cpu++) {
		for (uint32_t other = cpu + 1; other < seed->active_cpus; other++) {
			if (seed->participant_apic_ids[cpu] ==
			    seed->participant_apic_ids[other])
				return false;
		}
	}
	return true;
}

static void invocation_scrub(struct smm_invocation_evidence *evidence)
{
	for (uint32_t cpu = 0; cpu < SMM_INVOCATION_EVIDENCE_MAX_CPUS; cpu++)
		scrub(&evidence->participants[cpu],
			sizeof(evidence->participants[cpu]));
	evidence->arrived_cpus = 0;
	evidence->departed_cpus = 0;
	evidence->sentinel = 0;
	evidence->original_rax = 0;
	evidence->command = 0;
	evidence->command_reserved = 0;
	scrub(&evidence->token, sizeof(evidence->token));
	__atomic_store_n(&evidence->close_requested, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&evidence->reentry_detected, 0U, __ATOMIC_RELAXED);
	__atomic_store_n(&evidence->arrival_failed, 0U, __ATOMIC_RELAXED);
}

static void terminal_scrub(struct smm_invocation_evidence *evidence)
{
	invocation_scrub(evidence);
	evidence->active_cpus = 0;
	evidence->bsp_cpu = 0;
	evidence->boot_generation = 0;
	evidence->expected_cpus = 0;
	evidence->generation = 0;
	evidence->arrival_writers = 0;
	evidence->departure_writers = 0;
	scrub(evidence->participant_apic_ids,
		sizeof(evidence->participant_apic_ids));
	evidence->fail_stop = NULL;
	scrub(evidence->fail_context, sizeof(evidence->fail_context));
	evidence->fail_context_size = 0;
}

static void poison_finish_if_quiescent(
	struct smm_invocation_evidence *evidence)
{
	if (__atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->departure_writers, __ATOMIC_ACQUIRE))
		return;
	if (!phase_claim(evidence, SMM_INVOCATION_POISONING,
		SMM_INVOCATION_POISON_CLEANING))
		return;
	terminal_scrub(evidence);
	phase_publish(evidence, SMM_INVOCATION_POISONED);
}

static enum cb_err poison_owned(struct smm_invocation_evidence *evidence,
	uint32_t owned_phase)
{
	if (!phase_claim(evidence, owned_phase, SMM_INVOCATION_POISONING))
		return CB_ERR;
	poison_finish_if_quiescent(evidence);
	return CB_ERR;
}

static void __noreturn invocation_fail_stop(
	struct smm_invocation_evidence *evidence)
{
	smm_invocation_fail_stop_fn stop = evidence->fail_stop;
	uint8_t context[SMM_INVOCATION_EVIDENCE_CONTEXT_MAX];
	const size_t size = evidence->fail_context_size;

	if (!stop || size > sizeof(context))
		__builtin_trap();
	memcpy(context, evidence->fail_context, size);
	stop(context);
	__builtin_trap();
}

enum cb_err smm_invocation_evidence_provision(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_seed *seed,
	smm_invocation_fail_stop_fn fail_stop, const void *fail_context,
	size_t fail_context_size)
{
	struct smm_invocation_loader_seed snapshot;

	if (!evidence || !seed || !fail_stop ||
	    fail_context_size > SMM_INVOCATION_EVIDENCE_CONTEXT_MAX ||
	    (fail_context_size && !fail_context) ||
	    ranges_overlap(evidence, sizeof(*evidence), seed, sizeof(*seed)) ||
	    (fail_context_size &&
	     (ranges_overlap(evidence, sizeof(*evidence), fail_context,
		fail_context_size) ||
	      ranges_overlap(seed, sizeof(*seed), fail_context,
		fail_context_size))))
		return CB_ERR;
	memcpy(&snapshot, seed, sizeof(snapshot));
	if (!seed_valid(&snapshot) ||
	    !phase_claim(evidence, SMM_INVOCATION_EMPTY,
		SMM_INVOCATION_PROVISIONING)) {
		scrub(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	if (memcmp(seed, &snapshot, sizeof(snapshot))) {
		scrub(&snapshot, sizeof(snapshot));
		terminal_scrub(evidence);
		phase_publish(evidence, SMM_INVOCATION_POISONED);
		return CB_ERR;
	}

	terminal_scrub(evidence);
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		scrub(&snapshot, sizeof(snapshot));
		phase_publish(evidence, SMM_INVOCATION_CLOSED);
		return CB_ERR;
	}
	evidence->active_cpus = snapshot.active_cpus;
	evidence->bsp_cpu = snapshot.bsp_cpu;
	evidence->boot_generation = snapshot.boot_generation;
	memcpy(evidence->participant_apic_ids, snapshot.participant_apic_ids,
		snapshot.active_cpus * sizeof(snapshot.participant_apic_ids[0]));
	evidence->expected_cpus = cpu_mask(snapshot.active_cpus);
	evidence->fail_stop = fail_stop;
	if (fail_context_size)
		memcpy(evidence->fail_context, fail_context, fail_context_size);
	evidence->fail_context_size = fail_context_size;
	scrub(&snapshot, sizeof(snapshot));
	phase_publish(evidence, SMM_INVOCATION_READY);
	return CB_SUCCESS;
}

static enum cb_err invocation_open_owned(
	struct smm_invocation_evidence *evidence)
{
	invocation_scrub(evidence);
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE) ||
	    evidence->generation == UINT64_MAX)
		return poison_owned(evidence, SMM_INVOCATION_OPENING);
	evidence->generation++;
	if (!evidence->generation)
		return poison_owned(evidence, SMM_INVOCATION_OPENING);
	phase_publish(evidence, SMM_INVOCATION_COLLECTING);
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_arrive(
	struct smm_invocation_evidence *evidence, uint32_t cpu, uint32_t apic_id,
	uint64_t *generation)
{
	struct smm_invocation_participant *participant;
	uint32_t empty = SMM_INVOCATION_PARTICIPANT_EMPTY;
	uint32_t phase;
	bool opening_owner = false;
	bool failed;

	if (!evidence || !generation ||
	    !range_valid(generation, sizeof(*generation)) ||
	    ranges_overlap(evidence, sizeof(*evidence), generation,
		sizeof(*generation)))
		return CB_ERR;
	for (;;) {
		phase = phase_load(evidence);
		if (phase == SMM_INVOCATION_READY) {
			if (!phase_claim(evidence, SMM_INVOCATION_READY,
				SMM_INVOCATION_OPENING))
				continue;
			opening_owner = true;
			TEST_HOOK(1);
			break;
		}
		if (phase == SMM_INVOCATION_OPENING) {
			__asm__ volatile ("pause");
			continue;
		}
		if (phase == SMM_INVOCATION_ARRIVAL_ADMITTING) {
			__asm__ volatile ("pause");
			continue;
		}
		if (phase != SMM_INVOCATION_COLLECTING)
			return CB_ERR;
		if (!phase_claim(evidence, SMM_INVOCATION_COLLECTING,
			SMM_INVOCATION_ARRIVAL_ADMITTING))
			continue;
		TEST_HOOK(5);
		break;
	}
	__atomic_fetch_add(&evidence->arrival_writers, 1U, __ATOMIC_ACQ_REL);
	if (opening_owner && invocation_open_owned(evidence) != CB_SUCCESS) {
		__atomic_store_n(&evidence->arrival_failed, 1U, __ATOMIC_RELEASE);
		__atomic_fetch_sub(&evidence->arrival_writers, 1U,
			__ATOMIC_RELEASE);
		poison_finish_if_quiescent(evidence);
		return CB_ERR;
	}
	if (!opening_owner)
		phase_publish(evidence, SMM_INVOCATION_COLLECTING);
	phase = phase_load(evidence);
	if (phase != SMM_INVOCATION_COLLECTING ||
	    cpu >= evidence->active_cpus ||
	    apic_id != evidence->participant_apic_ids[cpu]) {
		__atomic_store_n(&evidence->arrival_failed, 1U, __ATOMIC_RELEASE);
		(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);
		__atomic_fetch_sub(&evidence->arrival_writers, 1U,
			__ATOMIC_RELEASE);
		poison_finish_if_quiescent(evidence);
		return CB_ERR;
	}

	participant = &evidence->participants[cpu];
	if (!__atomic_compare_exchange_n(&participant->phase, &empty,
		SMM_INVOCATION_PARTICIPANT_WRITING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE)) {
		__atomic_store_n(&evidence->arrival_failed, 1U, __ATOMIC_RELEASE);
		__atomic_fetch_sub(&evidence->arrival_writers, 1U,
			__ATOMIC_ACQ_REL);
		(void)poison_owned(evidence, SMM_INVOCATION_COLLECTING);
		poison_finish_if_quiescent(evidence);
		return CB_ERR;
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
	while (__atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	while (phase_load(evidence) == SMM_INVOCATION_ARRIVAL_ADMITTING)
		__asm__ volatile ("pause");
	failed = __atomic_load_n(&evidence->arrival_failed, __ATOMIC_ACQUIRE);
	poison_finish_if_quiescent(evidence);
	TEST_HOOK(8);
	if (failed ||
	    __atomic_load_n(&evidence->arrival_failed, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		*generation = 0;
		return CB_ERR;
	}
	return CB_SUCCESS;
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
	return __atomic_load_n(&evidence->reentry_detected, __ATOMIC_ACQUIRE);
}

static void record_reentry(struct smm_invocation_evidence *evidence,
	uint32_t phase)
{
	if (phase == SMM_INVOCATION_CLAIMING ||
	    phase == SMM_INVOCATION_PUBLISHING ||
	    phase == SMM_INVOCATION_ABORTING)
		__atomic_store_n(&evidence->reentry_detected, 1U,
			__ATOMIC_RELEASE);
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
		invocation_fail_stop(evidence);
	unchanged = ops_unchanged(source, ops) && !callback_reentered(evidence);
	if (ops->read_rax(ops->context, initiator, &readback) != CB_SUCCESS)
		invocation_fail_stop(evidence);
	unchanged = unchanged && ops_unchanged(source, ops) &&
		!callback_reentered(evidence);
	if (readback != evidence->original_rax)
		invocation_fail_stop(evidence);
	return unchanged;
}

static enum cb_err close_owned(struct smm_invocation_evidence *evidence,
	uint32_t owned_phase)
{
	__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
	if (!phase_claim(evidence, owned_phase, SMM_INVOCATION_CLOSING))
		return CB_ERR;
	return CB_ERR;
}

static void build_token(struct smm_invocation_evidence *evidence,
	uint32_t initiator, uint8_t command, uint64_t sentinel,
	struct smm_invocation_token *token)
{
	uint64_t proof[3] = {
		evidence->boot_generation ^ sentinel,
		evidence->generation ^ command,
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
			mix64(proof[0] ^ evidence->boot_generation),
			mix64(proof[1] ^ sentinel),
			mix64(proof[2] ^ command),
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
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&evidence->arrived_cpus, __ATOMIC_ACQUIRE) !=
	    evidence->expected_cpus ||
	    !phase_claim(evidence, SMM_INVOCATION_COLLECTING,
		SMM_INVOCATION_CLAIMING))
		return CB_ERR;
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
		(void)phase_claim(evidence, SMM_INVOCATION_CLAIMING,
			SMM_INVOCATION_CLOSING);
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
		if (__atomic_load_n(&evidence->shutdown_requested,
			__ATOMIC_ACQUIRE))
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
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE))
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
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot, initiator))
			return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
		__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
		(void)phase_claim(evidence, SMM_INVOCATION_CLAIMING,
			SMM_INVOCATION_CLOSING);
		return CB_ERR;
	}

	build_token(evidence, initiator, command, sentinel, &local_token);
	evidence->sentinel = sentinel;
	evidence->command = command;
	evidence->token = local_token;
	TEST_HOOK(2);
	if (callback_reentered(evidence) ||
	    __atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		(void)restore_or_fail_stop(evidence, ops, &snapshot, initiator);
		return poison_owned(evidence, SMM_INVOCATION_CLAIMING);
	}
	if (!phase_claim(evidence, SMM_INVOCATION_CLAIMING,
		SMM_INVOCATION_CLAIMED)) {
		(void)restore_or_fail_stop(evidence, ops, &snapshot, initiator);
		invocation_fail_stop(evidence);
	}
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot, initiator))
			return poison_owned(evidence, SMM_INVOCATION_CLAIMED);
		__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
		if (!phase_claim(evidence, SMM_INVOCATION_CLAIMED,
			SMM_INVOCATION_CLOSING))
			invocation_fail_stop(evidence);
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
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		if (!restore_or_fail_stop(evidence, ops, &snapshot,
			token_snapshot.initiator_cpu))
			return poison_owned(evidence,
				SMM_INVOCATION_PUBLISHING);
		__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
		if (!phase_claim(evidence, SMM_INVOCATION_PUBLISHING,
			SMM_INVOCATION_CLOSING))
			invocation_fail_stop(evidence);
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
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
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
		invocation_fail_stop(evidence);
	TEST_HOOK(4);
	if (!phase_claim(evidence, SMM_INVOCATION_PUBLISHING,
		SMM_INVOCATION_PUBLISHED))
		invocation_fail_stop(evidence);
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		if (!phase_claim(evidence, SMM_INVOCATION_PUBLISHED,
			SMM_INVOCATION_COMPLETING))
			invocation_fail_stop(evidence);
		__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
		phase_publish(evidence, SMM_INVOCATION_CLOSING);
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
	__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
	phase_publish(evidence, SMM_INVOCATION_CLOSING);
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
	__atomic_store_n(&evidence->close_requested, 1U, __ATOMIC_RELEASE);
	phase_publish(evidence, SMM_INVOCATION_CLOSING);
	scrub(&ops_snapshot, sizeof(ops_snapshot));
	scrub(&token_snapshot, sizeof(token_snapshot));
	return CB_SUCCESS;
}

static void close_finish_if_quiescent(
	struct smm_invocation_evidence *evidence)
{
	if (__atomic_load_n(&evidence->departure_writers, __ATOMIC_ACQUIRE) ||
	    !phase_claim(evidence, SMM_INVOCATION_CLOSE_CLEANING,
		SMM_INVOCATION_CLOSE_SCRUBBING))
		return;

	invocation_scrub(evidence);
	if (__atomic_load_n(&evidence->shutdown_requested, __ATOMIC_ACQUIRE)) {
		terminal_scrub(evidence);
		phase_publish(evidence, SMM_INVOCATION_CLOSED);
	} else {
		phase_publish(evidence, SMM_INVOCATION_READY);
	}
}

enum cb_err smm_invocation_evidence_depart(
	struct smm_invocation_evidence *evidence, uint32_t cpu,
	uint64_t generation)
{
	uint64_t old;
	uint32_t phase;

	if (!evidence)
		return CB_ERR;
	for (;;) {
		phase = phase_load(evidence);
		if (phase == SMM_INVOCATION_DEPARTURE_ADMITTING) {
			__asm__ volatile ("pause");
			continue;
		}
		if (phase != SMM_INVOCATION_CLOSING)
			return CB_ERR;
		if (!phase_claim(evidence, SMM_INVOCATION_CLOSING,
			SMM_INVOCATION_DEPARTURE_ADMITTING))
			continue;
		TEST_HOOK(6);
		break;
	}
	__atomic_fetch_add(&evidence->departure_writers, 1U, __ATOMIC_ACQ_REL);
	phase_publish(evidence, SMM_INVOCATION_CLOSING);
	TEST_HOOK(3);
	phase = SMM_INVOCATION_CLOSING;
	if (phase != SMM_INVOCATION_CLOSING || cpu >= evidence->active_cpus ||
	    generation != evidence->generation) {
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		close_finish_if_quiescent(evidence);
		poison_finish_if_quiescent(evidence);
		return CB_ERR;
	}
	old = __atomic_fetch_or(&evidence->departed_cpus,
		1ULL << cpu, __ATOMIC_ACQ_REL);
	if (old & (1ULL << cpu)) {
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		(void)poison_owned(evidence, SMM_INVOCATION_CLOSING);
		close_finish_if_quiescent(evidence);
		poison_finish_if_quiescent(evidence);
		return CB_ERR;
	}
	if ((old | (1ULL << cpu)) != evidence->expected_cpus) {
		__atomic_fetch_sub(&evidence->departure_writers, 1U,
			__ATOMIC_RELEASE);
		close_finish_if_quiescent(evidence);
		return CB_SUCCESS;
	}
	__atomic_fetch_sub(&evidence->departure_writers, 1U, __ATOMIC_RELEASE);
	if (!__atomic_load_n(&evidence->close_requested, __ATOMIC_ACQUIRE) ||
	    !phase_claim(evidence, SMM_INVOCATION_CLOSING,
		SMM_INVOCATION_CLOSE_CLEANING))
		return poison_owned(evidence, SMM_INVOCATION_CLOSING);
	close_finish_if_quiescent(evidence);
	return CB_SUCCESS;
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
		if (phase > SMM_INVOCATION_POISONED)
			invocation_fail_stop(evidence);
		if (phase == SMM_INVOCATION_CLOSED)
			return CB_SUCCESS;
		if (phase == SMM_INVOCATION_POISONED)
			return CB_ERR;
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
				invocation_fail_stop(evidence);
			terminal_scrub(evidence);
			phase_publish(evidence, SMM_INVOCATION_CLOSED);
			return CB_SUCCESS;
		}
		__atomic_store_n(&evidence->shutdown_requested, 1U,
			__ATOMIC_RELEASE);
		if (phase == SMM_INVOCATION_CLAIMING ||
		    phase == SMM_INVOCATION_PUBLISHING ||
		    phase == SMM_INVOCATION_ABORTING) {
			record_reentry(evidence, phase);
			return CB_ERR;
		}
		if (phase == SMM_INVOCATION_CLAIMED)
			invocation_fail_stop(evidence);
		if (phase == SMM_INVOCATION_COLLECTING &&
		    !__atomic_load_n(&evidence->arrival_writers, __ATOMIC_ACQUIRE) &&
		    __atomic_load_n(&evidence->arrived_cpus, __ATOMIC_ACQUIRE) ==
			evidence->expected_cpus) {
			__atomic_store_n(&evidence->close_requested, 1U,
				__ATOMIC_RELEASE);
			if (phase_claim(evidence, phase, SMM_INVOCATION_CLOSING))
				continue;
			continue;
		}
		if (phase == SMM_INVOCATION_PUBLISHED &&
		    phase_claim(evidence, phase, SMM_INVOCATION_COMPLETING)) {
			__atomic_store_n(&evidence->close_requested, 1U,
				__ATOMIC_RELEASE);
			phase_publish(evidence, SMM_INVOCATION_CLOSING);
			continue;
		}
		if (++spins == 10000000U)
			invocation_fail_stop(evidence);
		__asm__ volatile ("pause");
	}
}
