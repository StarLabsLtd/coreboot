/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_entry.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_entry_test_hook(uint32_t point, uint32_t cpu);
#define ENTRY_TEST_HOOK(point, cpu) smm_invocation_entry_test_hook(point, cpu)
#else
#define ENTRY_TEST_HOOK(point, cpu) do { } while (0)
#endif

static void __noreturn entry_fail_stop(void)
{
	smm_invocation_platform_fail_stop();
}

static void __noreturn entry_fail_stop_admitted(
	struct smm_invocation_evidence *evidence, uint64_t generation)
{
	(void)smm_invocation_evidence_rendezvous_fail(evidence, generation);
	entry_fail_stop();
}

static void rendezvous_ack_or_reset(struct smm_invocation_evidence *evidence,
	uint64_t generation, uint32_t cpu,
	const struct smm_invocation_entry_policy *policy)
{
	struct smm_invocation_admission_token token;
	enum smm_invocation_try_result result;

	for (uint32_t poll = 0; ; poll++) {
		result = smm_invocation_evidence_rendezvous_ack_try(evidence,
			generation, cpu, &token);
		if (result == SMM_INVOCATION_TRY_SUCCESS)
			return;
		if (result != SMM_INVOCATION_TRY_RETRY)
			entry_fail_stop_admitted(evidence, generation);
		if (poll + 1U == policy->max_polls) {
			(void)smm_invocation_evidence_admission_fail(evidence,
				&token);
			entry_fail_stop();
		}
		__asm__ volatile ("pause");
	}
}

#ifdef __TEST__
void smm_invocation_entry_test_ack_wait(
	struct smm_invocation_evidence *evidence, uint64_t generation,
	uint32_t cpu, const struct smm_invocation_entry_policy *policy)
{
	rendezvous_ack_or_reset(evidence, generation, cpu, policy);
}
#endif

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

static bool cause_valid(const struct smm_invocation_entry_cause *cause,
	struct smm_invocation_loader_instance_nonce nonce, uint8_t command)
{
	static const uint8_t zero[sizeof(cause->reserved)];

	return cause && cause->revision == SMM_INVOCATION_ENTRY_CAUSE_REVISION &&
		cause->size == sizeof(*cause) &&
		smm_invocation_loader_instance_nonce_equal(
			cause->loader_instance_nonce, nonce) &&
		(cause->lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD ||
		 cause->lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD) &&
		cause->command == command && cause->recognized == 1U &&
		!memcmp(cause->reserved, zero, sizeof(zero));
}

static bool policy_valid(const struct smm_invocation_entry_policy *policy)
{
	return policy &&
		policy->revision == SMM_INVOCATION_ENTRY_POLICY_REVISION &&
		policy->size == sizeof(*policy) && policy->max_polls &&
		policy->max_polls <= SMM_INVOCATION_ENTRY_MAX_POLLS &&
		!policy->reserved;
}

static bool ticket_valid(const struct smm_invocation_entry_ticket *ticket)
{
	static const uint8_t zero[sizeof(ticket->reserved)];

	return ticket && ticket->generation &&
		!smm_invocation_loader_instance_nonce_is_zero(
			ticket->loader_instance_nonce) &&
		(ticket->lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD ||
		 ticket->lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD) &&
		ticket->max_polls &&
		ticket->max_polls <= SMM_INVOCATION_ENTRY_MAX_POLLS &&
		!memcmp(ticket->reserved, zero, sizeof(zero));
}

static bool input_ranges_valid(struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	struct smm_invocation_entry_ticket *ticket)
{
	if (!range_valid(evidence, sizeof(*evidence)) ||
	    !range_valid(cause, sizeof(*cause)) ||
	    !range_valid(policy, sizeof(*policy)) ||
	    !range_valid(ticket, sizeof(*ticket)) ||
	    ranges_overlap(evidence, sizeof(*evidence), cause, sizeof(*cause)) ||
	    ranges_overlap(evidence, sizeof(*evidence), policy,
		sizeof(*policy)) ||
	    ranges_overlap(evidence, sizeof(*evidence), ticket, sizeof(*ticket)) ||
	    ranges_overlap(cause, sizeof(*cause), policy,
		sizeof(*policy)) ||
	    ranges_overlap(cause, sizeof(*cause), ticket, sizeof(*ticket)) ||
	    ranges_overlap(policy, sizeof(*policy), ticket,
		sizeof(*ticket)))
		return false;
	return true;
}

static bool sources_unchanged(const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_cause *cause_snapshot,
	const struct smm_invocation_entry_policy *policy,
	const struct smm_invocation_entry_policy *policy_snapshot)
{
	return !memcmp(cause, cause_snapshot, sizeof(*cause_snapshot)) &&
		!memcmp(policy, policy_snapshot, sizeof(*policy_snapshot));
}

enum cb_err smm_invocation_entry_arrive(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_cause *cause,
	const struct smm_invocation_entry_policy *policy,
	struct smm_invocation_loader_instance_nonce expected_loader_instance_nonce,
	uint8_t expected_command,
	uint32_t cpu, uint32_t initial_apic_id,
	struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_cause snapshot;
	struct smm_invocation_entry_policy policy_snapshot;
	uint64_t generation;
	struct smm_invocation_entry_ticket ticket_snapshot;
	struct smm_invocation_admission_token admission;
	enum smm_invocation_try_result try_result;

	if (!evidence || !cause || !ticket ||
	    smm_invocation_loader_instance_nonce_is_zero(
		expected_loader_instance_nonce) ||
	    !range_valid(policy, sizeof(*policy)))
		return CB_ERR;
	memcpy(&policy_snapshot, policy, sizeof(policy_snapshot));
	ENTRY_TEST_HOOK(1, cpu);
	if (!policy_valid(&policy_snapshot) ||
	    !input_ranges_valid(evidence, cause, policy, ticket))
		return CB_ERR;
	memcpy(&snapshot, cause, sizeof(snapshot));
	if (!cause_valid(&snapshot, expected_loader_instance_nonce, expected_command) ||
	    !sources_unchanged(cause, &snapshot, policy, &policy_snapshot))
		return CB_ERR;
	for (uint32_t poll = 0; ; poll++) {
		try_result = smm_invocation_evidence_require_rendezvous_ack_try(
			evidence, snapshot.loader_instance_nonce, snapshot.lifecycle,
			&admission);
		if (try_result == SMM_INVOCATION_TRY_SUCCESS)
			break;
		if (try_result != SMM_INVOCATION_TRY_RETRY)
			return CB_ERR;
		if (poll + 1U == policy_snapshot.max_polls) {
			(void)smm_invocation_evidence_admission_fail(evidence,
				&admission);
			entry_fail_stop();
		}
		__asm__ volatile ("pause");
	}
	for (uint32_t poll = 0; ; poll++) {
		try_result = smm_invocation_evidence_arrive_try(evidence, cpu,
			initial_apic_id, &generation, &admission);
		if (try_result == SMM_INVOCATION_TRY_SUCCESS)
			break;
		if (try_result != SMM_INVOCATION_TRY_RETRY)
			entry_fail_stop();
		if (poll + 1U == policy_snapshot.max_polls) {
			(void)smm_invocation_evidence_admission_fail(evidence,
				&admission);
			entry_fail_stop();
		}
		__asm__ volatile ("pause");
	}
	ENTRY_TEST_HOOK(2, cpu);
	for (uint32_t poll = 0; poll < policy_snapshot.max_polls; poll++) {
		if (!sources_unchanged(cause, &snapshot, policy,
			&policy_snapshot))
			entry_fail_stop_admitted(evidence, generation);
		if (smm_invocation_evidence_rendezvous_ready(evidence, generation)) {
			break;
		}
		if (poll + 1U == policy_snapshot.max_polls)
			entry_fail_stop_admitted(evidence, generation);
		__asm__ volatile ("pause");
	}
	memset(&ticket_snapshot, 0, sizeof(ticket_snapshot));
	ticket_snapshot.generation = generation;
	ticket_snapshot.loader_instance_nonce = snapshot.loader_instance_nonce;
	ticket_snapshot.cpu = cpu;
	ticket_snapshot.lifecycle = snapshot.lifecycle;
	ticket_snapshot.max_polls = policy_snapshot.max_polls;
	ticket_snapshot.command = snapshot.command;
	memcpy(ticket, &ticket_snapshot, sizeof(ticket_snapshot));
	ENTRY_TEST_HOOK(3, cpu);
	if (!sources_unchanged(cause, &snapshot, policy, &policy_snapshot) ||
	    memcmp(ticket, &ticket_snapshot, sizeof(*ticket)))
		entry_fail_stop_admitted(evidence, generation);
	rendezvous_ack_or_reset(evidence, generation, cpu, &policy_snapshot);
	ENTRY_TEST_HOOK(4, cpu);
	for (uint32_t poll = 0; poll < policy_snapshot.max_polls; poll++) {
		if (smm_invocation_evidence_rendezvous_ack_ready(evidence,
			generation))
			return CB_SUCCESS;
		if (poll + 1U == policy_snapshot.max_polls)
			entry_fail_stop_admitted(evidence, generation);
		__asm__ volatile ("pause");
	}
	return CB_SUCCESS;
}

enum cb_err smm_invocation_entry_depart(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_ticket snapshot;
	enum smm_invocation_try_result result;

	if (!range_valid(evidence, sizeof(*evidence)) ||
	    !range_valid(ticket, sizeof(*ticket)) ||
	    ranges_overlap(evidence, sizeof(*evidence), ticket, sizeof(*ticket)))
		return CB_ERR;
	memcpy(&snapshot, ticket, sizeof(snapshot));
	if (!ticket_valid(&snapshot))
		return CB_ERR;
	ENTRY_TEST_HOOK(5, snapshot.cpu);
	if (memcmp(ticket, &snapshot, sizeof(snapshot))) {
		(void)smm_invocation_evidence_ticket_fail(evidence,
			snapshot.generation);
		entry_fail_stop();
	}
	for (uint32_t poll = 0; ; poll++) {
		result = smm_invocation_evidence_depart_try(evidence,
			snapshot.cpu, snapshot.generation);
		if (result == SMM_INVOCATION_TRY_SUCCESS)
			break;
		if (result != SMM_INVOCATION_TRY_RETRY ||
		    poll + 1U == snapshot.max_polls) {
			(void)smm_invocation_evidence_ticket_fail(evidence,
				snapshot.generation);
			entry_fail_stop();
		}
		__asm__ volatile ("pause");
	}
	return CB_SUCCESS;
}

bool smm_invocation_entry_eos_ready(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_entry_ticket *ticket)
{
	struct smm_invocation_entry_ticket snapshot;

	if (!range_valid(evidence, sizeof(*evidence)) ||
	    !range_valid(ticket, sizeof(*ticket)) ||
	    ranges_overlap(evidence, sizeof(*evidence), ticket, sizeof(*ticket)))
		return false;
	memcpy(&snapshot, ticket, sizeof(snapshot));
	ENTRY_TEST_HOOK(6, snapshot.cpu);
	if (!ticket_valid(&snapshot) ||
	    smm_invocation_evidence_phase(evidence) !=
		SMM_INVOCATION_READY)
		return false;
	ENTRY_TEST_HOOK(7, snapshot.cpu);
	if (memcmp(ticket, &snapshot, sizeof(snapshot))) {
		(void)smm_invocation_evidence_ticket_fail(evidence,
			snapshot.generation);
		entry_fail_stop();
	}
	return smm_invocation_evidence_eos_consume(evidence,
		snapshot.generation, snapshot.loader_instance_nonce, snapshot.lifecycle,
		snapshot.cpu);
}
