/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/x86/smm_command.h>
#include <intelblocks/smm_invocation_cause.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

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
	test_valid_and_order();
	test_non_private_short_circuit();
	test_status_and_command_failures();
	test_protected_fact_failures();
	test_invalid_inputs();
	return 0;
}
