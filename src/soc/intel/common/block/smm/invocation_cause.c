/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <commonlib/helpers.h>
#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <intelblocks/smm_invocation_cause.h>
#include <soc/pm.h>
#include <string.h>

#if defined(__TEST__)
void intel_smm_invocation_cause_test_hook(uint32_t point);
#define CAUSE_TEST_HOOK(point) intel_smm_invocation_cause_test_hook(point)
#else
#define CAUSE_TEST_HOOK(point) do { } while (0)
#endif

struct cause_sample {
	uint32_t smi_status;
	uint8_t command;
};

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	return first_base <= second_base + second_size - 1U &&
		second_base <= first_base + first_size - 1U;
}

static void scrub_bytes(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool inputs_valid(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const uint32_t *runtime_cpus)
{
	const void *objects[] = {
		composition, topology, instance, evidence, runtime_cpus,
	};
	const size_t sizes[] = {
		sizeof(*composition), sizeof(*topology), sizeof(*instance),
		sizeof(*evidence), sizeof(*runtime_cpus),
	};
	const size_t alignments[] = {
		_Alignof(*composition), _Alignof(*topology), _Alignof(*instance),
		_Alignof(*evidence), _Alignof(*runtime_cpus),
	};

	for (size_t object = 0; object < ARRAY_SIZE(objects); object++) {
		if (!object_valid(objects[object], sizes[object], alignments[object]))
			return false;
		for (size_t prior = 0; prior < object; prior++)
			if (objects_overlap(objects[object], sizes[object],
				objects[prior], sizes[prior]))
				return false;
	}
	return true;
}

static bool output_disjoint(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const uint32_t *runtime_cpus,
	const struct smm_invocation_entry_cause *cause)
{
	const void *inputs[] = {
		composition, topology, instance, evidence, runtime_cpus,
	};
	const size_t sizes[] = {
		sizeof(*composition), sizeof(*topology), sizeof(*instance),
		sizeof(*evidence), sizeof(*runtime_cpus),
	};
	if (!object_valid(cause, sizeof(*cause), _Alignof(*cause)))
		return false;
	for (size_t input = 0; input < ARRAY_SIZE(inputs); input++)
		if (objects_overlap(cause, sizeof(*cause), inputs[input],
			sizes[input]))
			return false;
	return true;
}

static struct cause_sample sample_cause(void)
{
	struct cause_sample sample = { 0 };

	sample.smi_status = inl(ACPI_BASE_ADDRESS + SMI_STS);
	sample.command = inb(APM_CNT);
	return sample;
}

static bool evidence_ready(const struct smm_invocation_evidence *evidence)
{
	return __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_READY;
}

static bool protected_facts_valid(
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence, uint32_t runtime_cpus)
{
	const uint64_t expected_cpus = topology->active_cpus == 64U ? UINT64_MAX :
		(1ULL << topology->active_cpus) - 1ULL;

	return evidence_ready(evidence) && runtime_cpus &&
		runtime_cpus == topology->active_cpus &&
		topology->bsp_cpu == 0U &&
		instance->lifecycle == evidence->loader_lifecycle &&
		smm_invocation_loader_instance_nonce_equal(
			instance->loader_instance_nonce,
			evidence->loader_instance_nonce) &&
		evidence->active_cpus == topology->active_cpus &&
		evidence->bsp_cpu == topology->bsp_cpu &&
		evidence->expected_cpus == expected_cpus &&
		!memcmp(evidence->participant_apic_ids,
			topology->initial_apic_ids,
			topology->active_cpus * sizeof(topology->initial_apic_ids[0])) &&
		evidence_ready(evidence);
}

enum intel_smm_invocation_cause_result __noinline
intel_smm_invocation_private_apmc_cause(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const uint32_t *runtime_cpus, uint8_t expected_command,
	struct smm_invocation_entry_cause *cause)
{
	struct smm_invocation_topology topology_a = { 0 };
	struct smm_invocation_topology topology_b = { 0 };
	struct smm_invocation_loader_instance instance_a = { 0 };
	struct smm_invocation_loader_instance instance_b = { 0 };
	const struct smm_invocation_evidence *evidence_a;
	const struct smm_invocation_evidence *evidence_b;
	struct cause_sample sample_a;
	struct cause_sample sample_b;
	uint32_t runtime_a = 0;
	uint32_t runtime_b = 0;
	bool facts_a_valid = false;
	bool facts_valid = false;
	const bool valid_inputs = inputs_valid(composition, topology, instance,
		evidence, runtime_cpus);
	const bool output_safe = valid_inputs && output_disjoint(composition,
		topology, instance, evidence, runtime_cpus, cause);
	struct smm_invocation_entry_cause cause_value = { 0 };
	enum intel_smm_invocation_cause_result result;

	sample_a = sample_cause();
	CAUSE_TEST_HOOK(1);
	if (sample_a.command != expected_command) {
		result = INTEL_SMM_INVOCATION_CAUSE_NOT_PRIVATE;
		goto publish;
	}
	if (output_safe) {
		evidence_a = smm_invocation_loader_composition_evidence(
			composition, evidence);
		CAUSE_TEST_HOOK(2);
		if (evidence_a == evidence &&
		    smm_invocation_topology_read(topology, &topology_a) == CB_SUCCESS &&
		    smm_invocation_loader_instance_read(instance, &instance_a) ==
			CB_SUCCESS) {
			runtime_a = __atomic_load_n(runtime_cpus, __ATOMIC_ACQUIRE);
			facts_a_valid = protected_facts_valid(&topology_a, &instance_a,
				evidence_a, runtime_a);
			CAUSE_TEST_HOOK(3);
			evidence_b = smm_invocation_loader_composition_evidence(
				composition, evidence);
			CAUSE_TEST_HOOK(4);
			if (facts_a_valid && evidence_b == evidence_a &&
			    smm_invocation_topology_read(topology, &topology_b) ==
				CB_SUCCESS &&
			    smm_invocation_loader_instance_read(instance, &instance_b) ==
				CB_SUCCESS) {
				runtime_b = __atomic_load_n(runtime_cpus,
					__ATOMIC_ACQUIRE);
				facts_valid = protected_facts_valid(&topology_b,
					&instance_b, evidence_b, runtime_b) &&
					!memcmp(&topology_a, &topology_b,
						sizeof(topology_a)) &&
					!memcmp(&instance_a, &instance_b,
						sizeof(instance_a)) &&
					runtime_a == runtime_b;
			}
		}
	}
	CAUSE_TEST_HOOK(5);
	sample_b = sample_cause();
	if (!facts_valid || sample_a.smi_status != sample_b.smi_status ||
	    sample_a.command != sample_b.command ||
	    sample_a.smi_status != (1U << APM_STS_BIT)) {
		result = INTEL_SMM_INVOCATION_CAUSE_PRIVATE_INVALID;
		goto publish;
	}

	cause_value = (struct smm_invocation_entry_cause) {
		.revision = SMM_INVOCATION_ENTRY_CAUSE_REVISION,
		.size = sizeof(*cause),
		.loader_instance_nonce = instance_a.loader_instance_nonce,
		.lifecycle = instance_a.lifecycle,
		.command = sample_a.command,
		.recognized = 1U,
	};
	result = INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID;

publish:
	if (output_safe) {
		if (result == INTEL_SMM_INVOCATION_CAUSE_PRIVATE_VALID)
			memcpy(cause, &cause_value, sizeof(*cause));
		else
			memset(cause, 0, sizeof(*cause));
	}
	scrub_bytes(&topology_a, sizeof(topology_a));
	scrub_bytes(&topology_b, sizeof(topology_b));
	scrub_bytes(&instance_a, sizeof(instance_a));
	scrub_bytes(&instance_b, sizeof(instance_b));
	scrub_bytes(&cause_value, sizeof(cause_value));
	return result;
}

enum intel_smm_invocation_cause_result intel_smm_invocation_private_cause(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const uint32_t *runtime_cpus,
	struct smm_invocation_entry_cause *cause)
{
	return intel_smm_invocation_private_apmc_cause(composition, topology,
		instance, evidence, runtime_cpus, SMM_APMC_AUTHVAR_PRESENCE, cause);
}
