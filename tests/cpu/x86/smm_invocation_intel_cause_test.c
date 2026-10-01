/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/x86/smm_command.h>
#include <intelblocks/smm_invocation_cause.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { \
	if (!(condition)) { \
		if (write(STDERR_FILENO, "failed: " #condition "\n", \
			sizeof("failed: " #condition "\n") - 1U) < 0) \
			abort(); \
		abort(); \
	} \
} while (0)

#define APM_STATUS (1U << 5)

enum mutation {
	MUTATE_NONE,
	MUTATE_COMPOSITION,
	MUTATE_TOPOLOGY,
	MUTATE_INSTANCE,
	MUTATE_EVIDENCE,
	MUTATE_RUNTIME_CPUS,
	MUTATE_EVIDENCE_PHASE,
	MUTATE_COMPOSITION_GATE_A,
};

static struct smm_invocation_loader_composition composition;
static struct smm_invocation_topology topology;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static uint32_t runtime_cpus;
static uint32_t smi_status[2];
static uint8_t apmc[2];
static uint32_t smi_reads;
static uint32_t apmc_reads;
static uint32_t io_writes;
static char events[64];
static size_t event_count;
static enum mutation mutation;
static uint32_t ready_drift;

void smm_invocation_evidence_test_hook(uint32_t point)
{
	if (point == 81U && ready_drift == 4U)
		evidence.active_cpus = UINT32_MAX;
	if (point != 80U)
		return;
	if (ready_drift == 1U)
		evidence.closed_generation++;
	else if (ready_drift == 2U)
		evidence.state ^= 1U << 9;
	else if (ready_drift == 3U)
		evidence.token.smi_generation = 1U;
}

void smm_invocation_platform_fail_stop(void)
{
	abort();
}

struct saved_values {
	uint32_t initiator;
	uint64_t values[2];
};

static enum smm_invocation_match match_write(void *context, uint32_t cpu,
	uint8_t command)
{
	const struct saved_values *saved = context;

	assert(cpu < 2U && command == SMM_APMC_AUTHVAR_SERVICE);
	return cpu == saved->initiator ? SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	const struct saved_values *saved = context;

	assert(cpu < 2U);
	*value = saved->values[cpu];
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	struct saved_values *saved = context;

	assert(cpu < 2U);
	saved->values[cpu] = value;
	return CB_SUCCESS;
}

static void record(char event)
{
	assert(event_count < sizeof(events));
	events[event_count++] = event;
}

uint32_t inl(uint16_t port)
{
	record('S');
	assert(port == 0x1834U);
	assert(smi_reads < 2U);
	return smi_status[smi_reads++];
}

uint8_t inb(uint16_t port)
{
	record('C');
	assert(port == 0xb2U);
	assert(apmc_reads < 2U);
	return apmc[apmc_reads++];
}

void outb(uint8_t value, uint16_t port)
{
	(void)value;
	(void)port;
	io_writes++;
}

void intel_smm_invocation_cause_test_hook(uint32_t point)
{
	record((char)('0' + point));
	if (mutation == MUTATE_COMPOSITION_GATE_A && point == 2U) {
		composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
		return;
	}
	if (mutation == MUTATE_EVIDENCE_PHASE && point == 4U) {
		evidence.state = SMM_INVOCATION_OPENING;
		return;
	}
	if (point != 3U)
		return;
	switch (mutation) {
	case MUTATE_COMPOSITION:
		composition.state = SMM_INVOCATION_LOADER_COMPOSITION_FAILED;
		break;
	case MUTATE_TOPOLOGY:
		topology.initial_apic_ids[1] ^= 1U;
		evidence.participant_apic_ids[1] = topology.initial_apic_ids[1];
		break;
	case MUTATE_INSTANCE:
		instance.loader_instance_nonce.high ^= 1U;
		evidence.loader_instance_nonce = instance.loader_instance_nonce;
		break;
	case MUTATE_EVIDENCE:
		evidence.loader_instance_nonce.low ^= 1U;
		break;
	case MUTATE_RUNTIME_CPUS:
		runtime_cpus++;
		break;
	case MUTATE_EVIDENCE_PHASE:
	case MUTATE_COMPOSITION_GATE_A:
	case MUTATE_NONE:
		break;
	}
}

void smm_invocation_topology_test_hook(uint32_t point)
{
	assert(point == 3U);
	record('T');
}

void smm_invocation_loader_instance_test_hook(uint32_t point)
{
	assert(point == 3U);
	record('I');
}

static void reset_fixture(void)
{
	memset(&composition, 0, sizeof(composition));
	memset(&topology, 0, sizeof(topology));
	memset(&instance, 0, sizeof(instance));
	memset(&evidence, 0, sizeof(evidence));
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.owner_attempt = 1U;
	composition.evidence_identity = (uintptr_t)&evidence;
	topology.state = SMM_INVOCATION_TOPOLOGY_READY;
	topology.revision = SMM_INVOCATION_TOPOLOGY_REVISION;
	topology.size = sizeof(topology);
	topology.active_cpus = 2U;
	topology.bsp_cpu = 0U;
	topology.initial_apic_ids[0] = 0U;
	topology.initial_apic_ids[1] = 2U;
	instance.state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance.size = sizeof(instance);
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	instance.loader_instance_nonce.low = 0x123456789abcdef0ULL;
	instance.loader_instance_nonce.high = 0xfedcba9876543210ULL;
	evidence.state = SMM_INVOCATION_READY;
	evidence.active_cpus = topology.active_cpus;
	evidence.bsp_cpu = topology.bsp_cpu;
	evidence.loader_instance_nonce = instance.loader_instance_nonce;
	evidence.loader_lifecycle = instance.lifecycle;
	evidence.expected_cpus = 3U;
	memcpy(evidence.participant_apic_ids, topology.initial_apic_ids,
		topology.active_cpus * sizeof(topology.initial_apic_ids[0]));
	runtime_cpus = topology.active_cpus;
	smi_status[0] = APM_STATUS;
	smi_status[1] = APM_STATUS;
	apmc[0] = SMM_APMC_AUTHVAR_PRESENCE;
	apmc[1] = SMM_APMC_AUTHVAR_PRESENCE;
	smi_reads = 0;
	apmc_reads = 0;
	io_writes = 0;
	event_count = 0;
	memset(events, 0, sizeof(events));
	mutation = MUTATE_NONE;
}

static enum intel_smm_invocation_cause_result classify(
	struct smm_invocation_entry_cause *cause)
{
	return intel_smm_invocation_private_cause(&composition, &topology,
		&instance, &evidence, &runtime_cpus, cause);
}

static enum intel_smm_invocation_cause_result classify_command(
	uint8_t command, struct smm_invocation_entry_cause *cause)
{
	return intel_smm_invocation_private_apmc_cause(&composition, &topology,
		&instance, &evidence, &runtime_cpus, command, cause);
}

static void expect_invalid(void)
{
	struct smm_invocation_entry_cause cause;
	enum intel_smm_invocation_cause_result result;

	memset(&cause, 0xa5, sizeof(cause));
	result = classify(&cause);
	assert(result == INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID);
	assert(!memcmp(&cause, &(struct smm_invocation_entry_cause) { 0 },
		sizeof(cause)));
	assert(io_writes == 0U);
}

static void test_valid_and_order(void)
{
	struct smm_invocation_entry_cause cause;
	static const char expected[] = "SC12TI34TI5SC";

	reset_fixture();
	assert(classify(&cause) == INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	assert(cause.revision == SMM_INVOCATION_ENTRY_CAUSE_REVISION);
	assert(cause.size == sizeof(cause));
	assert(cause.command == SMM_APMC_AUTHVAR_PRESENCE);
	assert(cause.recognized == 1U);
	assert(cause.lifecycle == instance.lifecycle);
	assert(smm_invocation_loader_instance_nonce_equal(
		cause.loader_instance_nonce, instance.loader_instance_nonce));
	assert(event_count == sizeof(expected) - 1U);
	assert(!memcmp(events, expected, sizeof(expected) - 1U));
	assert(smi_reads == 2U && apmc_reads == 2U && io_writes == 0U);
}

static void expect_next_classification(bool valid)
{
	struct smm_invocation_entry_cause cause;

	smi_reads = 0;
	apmc_reads = 0;
	event_count = 0;
	apmc[0] = apmc[1] = SMM_APMC_AUTHVAR_SERVICE;
	assert(classify_command(SMM_APMC_AUTHVAR_SERVICE, &cause) ==
		(valid ? INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID :
			INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID));
}

static void test_completed_successor(void)
{
	static const size_t bad_fields[] = {
		offsetof(struct smm_invocation_evidence, closed_generation),
		offsetof(struct smm_invocation_evidence, closed_loader_instance_nonce),
		offsetof(struct smm_invocation_evidence, closed_lifecycle),
		offsetof(struct smm_invocation_evidence, closed_eos_consumed),
		offsetof(struct smm_invocation_evidence, close_receipt_reserved),
		offsetof(struct smm_invocation_evidence, arrival_writers),
		offsetof(struct smm_invocation_evidence, departure_writers),
		offsetof(struct smm_invocation_evidence, arrival_failed),
		offsetof(struct smm_invocation_evidence, departure_failed),
		offsetof(struct smm_invocation_evidence, rendezvous_fail_requested),
		offsetof(struct smm_invocation_evidence, arrived_cpus),
		offsetof(struct smm_invocation_evidence, rendezvous_ack_cpus),
		offsetof(struct smm_invocation_evidence, departed_cpus),
		offsetof(struct smm_invocation_evidence, sentinel),
		offsetof(struct smm_invocation_evidence, original_value),
		offsetof(struct smm_invocation_evidence, command),
		offsetof(struct smm_invocation_evidence, token),
		offsetof(struct smm_invocation_evidence, participants),
		offsetof(struct smm_invocation_evidence, participant_apic_ids) + 2U * sizeof(uint32_t),
		offsetof(struct smm_invocation_evidence, rendezvous_ack_required),
	};
	static struct smm_invocation_evidence completed;
	struct smm_invocation_admission_token admission;
	struct smm_invocation_token token;
	struct saved_values saved;
	const struct smm_invocation_save_state_ops ops = {
		.match_apmc_write = match_write, .read_value = read_value,
		.write_value = write_value, .context = &saved, .context_size = sizeof(saved),
	};
	uint64_t generation;

	for (uint32_t initiator = 0; initiator < 2U; initiator++) {
		struct smm_invocation_loader_seed seed = {
			.revision = SMM_INVOCATION_EVIDENCE_REVISION, .size = sizeof(seed),
			.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD, .active_cpus = 2U,
			.participant_apic_ids = { 0U, 2U },
		};

		reset_fixture();
		seed.loader_instance_nonce = instance.loader_instance_nonce;
		memset(&evidence, 0, sizeof(evidence));
		assert(smm_invocation_evidence_provision(&evidence, &seed) == CB_SUCCESS);
		assert(smm_invocation_evidence_entry_ready(&evidence));
		for (uint32_t wave = 0; wave < 2U; wave++) {
			expect_next_classification(true);
			assert(smm_invocation_evidence_require_rendezvous_ack_try(&evidence,
				instance.loader_instance_nonce, instance.lifecycle, &admission) ==
				SMM_INVOCATION_TRY_SUCCESS);
			for (uint32_t cpu = 0; cpu < 2U; cpu++)
				assert(smm_invocation_evidence_arrive_try(&evidence, cpu,
					topology.initial_apic_ids[cpu], &generation, &admission) ==
					SMM_INVOCATION_TRY_SUCCESS);
			for (uint32_t cpu = 0; cpu < 2U; cpu++)
				assert(smm_invocation_evidence_rendezvous_ack_try(&evidence,
					generation, cpu, &admission) == SMM_INVOCATION_TRY_SUCCESS);
			saved = (struct saved_values) { .initiator = initiator };
			saved.values[initiator] = SMM_APMC_AUTHVAR_SERVICE;
			assert(smm_invocation_evidence_claim(&evidence, SMM_APMC_AUTHVAR_SERVICE,
				SMM_APMC_AUTHVAR_SERVICE, &ops, &token) == CB_SUCCESS);
			assert(token.initiator_cpu == initiator);
			assert(smm_invocation_evidence_publish_and_request_close(&evidence,
				&token, 0U, &ops) == CB_SUCCESS);
			assert(saved.values[initiator] == 0U);
			for (uint32_t cpu = 0; cpu < 2U; cpu++)
				assert(smm_invocation_evidence_depart_try(&evidence, cpu, generation) ==
					SMM_INVOCATION_TRY_SUCCESS);
			assert(!smm_invocation_evidence_entry_ready(&evidence));
			assert(smm_invocation_evidence_eos_consume(&evidence, generation,
				instance.loader_instance_nonce, instance.lifecycle, topology.bsp_cpu));
			assert(smm_invocation_evidence_entry_ready(&evidence));
		}
		completed = evidence;
		for (size_t field = 0; field < ARRAY_SIZE(bad_fields); field++) {
			((uint8_t *)&evidence)[bad_fields[field]] ^= 1U;
			assert(!smm_invocation_evidence_entry_ready(&evidence));
			expect_next_classification(false);
			evidence = completed;
		}
		for (uint32_t bit = 7U; bit < 12U; bit++) {
			evidence.state |= 1U << bit;
			assert(!smm_invocation_evidence_entry_ready(&evidence));
			expect_next_classification(false);
			evidence = completed;
		}
		evidence.state = SMM_INVOCATION_READY;
		expect_next_classification(false);
		evidence = completed;
		evidence.state &= 0xfffU;
		expect_next_classification(false);
		evidence = completed;
		evidence.state = (completed.state & 0xfffU) | 0xfffff000U;
		expect_next_classification(false);
		evidence = completed;
		evidence.state = (completed.state & ~0x60U) | 0x20U;
		expect_next_classification(false);
		evidence = completed;
		evidence.generation = evidence.closed_generation = UINT64_MAX;
		expect_next_classification(false);
		evidence = completed;
		for (uint32_t drift = 1U; drift <= 4U; drift++) {
			ready_drift = drift;
			assert(!smm_invocation_evidence_entry_ready(&evidence));
			evidence = completed;
		}
		ready_drift = 0;
		expect_next_classification(true);
	}
}

static void test_non_private_short_circuit(void)
{
	struct smm_invocation_entry_cause cause;
	static const char expected[] = "SC1";

	reset_fixture();
	apmc[0] = 0xe1U;
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_FAILED;
	memset(&cause, 0xa5, sizeof(cause));
	assert(classify(&cause) == INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE);
	assert(!memcmp(&cause, &(struct smm_invocation_entry_cause) { 0 },
		sizeof(cause)));
	assert(event_count == sizeof(expected) - 1U);
	assert(!memcmp(events, expected, sizeof(expected) - 1U));
	assert(smi_reads == 1U && apmc_reads == 1U && io_writes == 0U);
}

static void test_exact_command_selector(void)
{
	struct smm_invocation_entry_cause cause;

	reset_fixture();
	apmc[0] = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
	apmc[1] = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
	assert(classify_command(SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		&cause) == INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID);
	assert(cause.command == SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE);

	reset_fixture();
	assert(classify_command(SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		&cause) == INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE);

	reset_fixture();
	apmc[0] = SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE;
	apmc[1] = SMM_APMC_AUTHVAR_PRESENCE;
	assert(classify_command(SMM_APMC_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE,
		&cause) == INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID);
}

static void test_status_and_command_failures(void)
{
	for (uint32_t bit = 0; bit < 32U; bit++) {
		if (bit == 5U)
			continue;
		reset_fixture();
		smi_status[0] = APM_STATUS | (1U << bit);
		smi_status[1] = smi_status[0];
		expect_invalid();
	}

	reset_fixture();
	smi_status[0] = 0;
	smi_status[1] = 0;
	expect_invalid();

	reset_fixture();
	smi_status[1] |= 1U << 8;
	expect_invalid();

	reset_fixture();
	apmc[1] = 0xe1U;
	expect_invalid();
}

static void test_protected_fact_failures(void)
{
	reset_fixture();
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_FAILED;
	expect_invalid();

	reset_fixture();
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_FAILED;
	mutation = MUTATE_COMPOSITION_GATE_A;
	expect_invalid();

	for (enum mutation current = MUTATE_COMPOSITION;
	     current <= MUTATE_EVIDENCE_PHASE; current++) {
		reset_fixture();
		mutation = current;
		expect_invalid();
	}

	reset_fixture();
	evidence.active_cpus++;
	expect_invalid();

	reset_fixture();
	evidence.state = SMM_INVOCATION_OPENING;
	expect_invalid();

	reset_fixture();
	evidence.expected_cpus = 1U;
	expect_invalid();

	reset_fixture();
	evidence.participant_apic_ids[1] ^= 1U;
	expect_invalid();

	reset_fixture();
	runtime_cpus = 0U;
	expect_invalid();

	reset_fixture();
	instance.lifecycle = 99U;
	expect_invalid();
}

static void test_invalid_inputs(void)
{
	struct smm_invocation_entry_cause cause;
	struct smm_invocation_topology topology_before;

	reset_fixture();
	assert(intel_smm_invocation_private_cause(NULL, &topology, &instance,
		&evidence, &runtime_cpus, &cause) ==
		INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID);
	assert(io_writes == 0U);

	reset_fixture();
	assert(intel_smm_invocation_private_cause(&composition, &topology,
		&instance, &evidence, &runtime_cpus, NULL) ==
		INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID);
	assert(io_writes == 0U);

	reset_fixture();
	topology_before = topology;
	assert(intel_smm_invocation_private_cause(&composition, &topology,
		&instance, &evidence, &runtime_cpus,
		(struct smm_invocation_entry_cause *)&topology) ==
		INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID);
	assert(!memcmp(&topology, &topology_before, sizeof(topology)));
	assert(io_writes == 0U);
}

int main(void)
{
	test_completed_successor();
	test_valid_and_order();
	test_non_private_short_circuit();
	test_exact_command_selector();
	test_status_and_command_failures();
	test_protected_fact_failures();
	test_invalid_inputs();
	return 0;
}
