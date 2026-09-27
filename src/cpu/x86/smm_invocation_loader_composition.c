/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_loader_composition.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_loader_composition_test_hook(uint32_t point);
#define COMPOSITION_TEST_HOOK(point) \
	smm_invocation_loader_composition_test_hook(point)
#else
#define COMPOSITION_TEST_HOOK(point) do { } while (0)
#endif

static void scrub_bytes(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

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

static bool inputs_valid(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence)
{
	return object_valid(composition, sizeof(*composition),
			_Alignof(*composition)) &&
		object_valid(topology, sizeof(*topology), _Alignof(*topology)) &&
		object_valid(instance, sizeof(*instance), _Alignof(*instance)) &&
		object_valid(evidence, sizeof(*evidence), _Alignof(*evidence)) &&
		!objects_overlap(composition, sizeof(*composition), topology,
			sizeof(*topology)) &&
		!objects_overlap(composition, sizeof(*composition), instance,
			sizeof(*instance)) &&
		!objects_overlap(composition, sizeof(*composition), evidence,
			sizeof(*evidence)) &&
		!objects_overlap(topology, sizeof(*topology), instance,
			sizeof(*instance)) &&
		!objects_overlap(topology, sizeof(*topology), evidence,
			sizeof(*evidence)) &&
		!objects_overlap(instance, sizeof(*instance), evidence,
			sizeof(*evidence));
}

static int claim_composition(
	struct smm_invocation_loader_composition *composition,
	uint32_t *owner_attempt)
{
	const struct smm_invocation_loader_composition empty = { 0 };
	uint32_t expected = SMM_INVOCATION_LOADER_COMPOSITION_EMPTY;
	bool dirty;

	if (!__atomic_compare_exchange_n(&composition->state, &expected,
		SMM_INVOCATION_LOADER_COMPOSITION_COMPOSING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return 0;
	dirty = memcmp((const uint8_t *)composition + sizeof(composition->state),
		(const uint8_t *)&empty + sizeof(empty.state),
		sizeof(empty) - sizeof(empty.state));
	scrub_bytes((uint8_t *)composition + sizeof(composition->state),
		sizeof(*composition) - sizeof(composition->state));
	composition->owner_attempt = 1U;
	composition->reserved[0] = 0;
	composition->reserved[1] = 0;
	*owner_attempt = 1U;
	return dirty ? -1 : 1;
}

static bool evidence_matches_seed(
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_seed *seed)
{
	struct smm_invocation_evidence expected = { 0 };
	bool matches;

	expected.state = SMM_INVOCATION_READY;
	expected.active_cpus = seed->active_cpus;
	expected.bsp_cpu = seed->bsp_cpu;
	expected.loader_instance_nonce = seed->loader_instance_nonce;
	expected.loader_lifecycle = seed->lifecycle;
	memcpy(expected.participant_apic_ids, seed->participant_apic_ids,
		sizeof(expected.participant_apic_ids));
	expected.expected_cpus = seed->active_cpus == 64U ? UINT64_MAX :
		(1ULL << seed->active_cpus) - 1ULL;
	matches = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_READY &&
		!memcmp(evidence, &expected, sizeof(expected)) &&
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_READY;
	scrub_bytes(&expected, sizeof(expected));
	return matches;
}

static bool owner_valid(
	const struct smm_invocation_loader_composition *composition,
	uint32_t owner_attempt, const struct smm_invocation_evidence *evidence)
{
	return __atomic_load_n(&composition->state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_LOADER_COMPOSITION_COMPOSING && owner_attempt &&
		composition->owner_attempt == owner_attempt &&
		!composition->reserved[0] && !composition->reserved[1] &&
		composition->evidence_identity == (uint64_t)(uintptr_t)evidence;
}

static void build_evidence_seed(
	struct smm_invocation_loader_seed *seed,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance)
{
	memset(seed, 0, sizeof(*seed));
	seed->revision = SMM_INVOCATION_EVIDENCE_REVISION;
	seed->size = sizeof(*seed);
	seed->active_cpus = topology->active_cpus;
	seed->bsp_cpu = topology->bsp_cpu;
	seed->loader_instance_nonce = instance->loader_instance_nonce;
	memcpy(seed->participant_apic_ids, topology->initial_apic_ids,
		topology->active_cpus * sizeof(seed->participant_apic_ids[0]));
	seed->lifecycle = instance->lifecycle;
}

enum cb_err smm_invocation_loader_compose(
	struct smm_invocation_loader_composition *composition,
	struct smm_invocation_topology *topology,
	struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence)
{
	struct smm_invocation_topology topology_snapshot = { 0 };
	struct smm_invocation_topology topology_check = { 0 };
	struct smm_invocation_loader_instance_seed instance_seed = { 0 };
	struct smm_invocation_loader_instance instance_snapshot = { 0 };
	struct smm_invocation_loader_instance instance_check = { 0 };
	struct smm_invocation_loader_seed evidence_seed = { 0 };
	uint32_t owner_attempt = 0;
	uint32_t expected;
	int claim;
	bool instance_published = false;
	struct smm_invocation_evidence_loader_receipt evidence_receipt = { 0 };
	bool unwind = true;

	if (!inputs_valid(composition, topology, instance, evidence))
		return CB_ERR_ARG;
	claim = claim_composition(composition, &owner_attempt);
	if (!claim)
		return CB_ERR;
	if (claim < 0)
		goto fail;
	composition->evidence_identity = (uint64_t)(uintptr_t)evidence;
	COMPOSITION_TEST_HOOK(1);
	if (!owner_valid(composition, owner_attempt, evidence) ||
	    smm_invocation_topology_read(topology, &topology_snapshot) !=
		CB_SUCCESS)
		goto fail;
	COMPOSITION_TEST_HOOK(2);
	if (!owner_valid(composition, owner_attempt, evidence) ||
	    smm_invocation_platform_loader_instance_take(&instance_seed) !=
		CB_SUCCESS)
		goto fail;
	COMPOSITION_TEST_HOOK(3);
	if (!owner_valid(composition, owner_attempt, evidence) ||
	    smm_invocation_loader_instance_publish(instance, &instance_seed) !=
		CB_SUCCESS)
		goto fail;
	instance_published = true;
	COMPOSITION_TEST_HOOK(4);
	if (!owner_valid(composition, owner_attempt, evidence) ||
	    smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS ||
	    memcmp(&topology_snapshot, &topology_check,
		sizeof(topology_snapshot)) ||
	    smm_invocation_loader_instance_read(instance, &instance_snapshot) !=
		CB_SUCCESS ||
	    instance_snapshot.lifecycle != instance_seed.lifecycle ||
	    !smm_invocation_loader_instance_nonce_equal(
		instance_snapshot.loader_instance_nonce,
		instance_seed.loader_instance_nonce))
		goto fail;
	COMPOSITION_TEST_HOOK(5);
	if (!owner_valid(composition, owner_attempt, evidence))
		goto fail;
	build_evidence_seed(&evidence_seed, &topology_snapshot,
		&instance_snapshot);
	if (smm_invocation_evidence_loader_provision(evidence, &evidence_seed,
		&evidence_receipt) != CB_SUCCESS)
		goto fail;
	COMPOSITION_TEST_HOOK(6);
	scrub_bytes(&topology_check, sizeof(topology_check));
	scrub_bytes(&instance_check, sizeof(instance_check));
	if (!owner_valid(composition, owner_attempt, evidence) ||
	    smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS ||
	    memcmp(&topology_snapshot, &topology_check,
		sizeof(topology_snapshot)) ||
	    smm_invocation_loader_instance_read(instance, &instance_check) !=
		CB_SUCCESS ||
	    memcmp(&instance_snapshot, &instance_check,
		sizeof(instance_snapshot)))
		goto fail;
	COMPOSITION_TEST_HOOK(7);
	scrub_bytes(&topology_check, sizeof(topology_check));
	scrub_bytes(&instance_check, sizeof(instance_check));
	if (!owner_valid(composition, owner_attempt, evidence) ||
	    smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS ||
	    memcmp(&topology_snapshot, &topology_check,
		sizeof(topology_snapshot)) ||
	    smm_invocation_loader_instance_read(instance, &instance_check) !=
		CB_SUCCESS ||
	    memcmp(&instance_snapshot, &instance_check,
		sizeof(instance_snapshot)) ||
	    !evidence_matches_seed(evidence, &evidence_seed))
		goto fail;
	expected = SMM_INVOCATION_LOADER_COMPOSITION_COMPOSING;
	if (!__atomic_compare_exchange_n(&composition->state, &expected,
		SMM_INVOCATION_LOADER_COMPOSITION_READY, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		/* A stale owner must never tear down an observed READY bundle. */
		if (expected == SMM_INVOCATION_LOADER_COMPOSITION_READY)
			unwind = false;
		goto fail;
	}
	scrub_bytes(&topology_snapshot, sizeof(topology_snapshot));
	scrub_bytes(&topology_check, sizeof(topology_check));
	scrub_bytes(&instance_seed, sizeof(instance_seed));
	scrub_bytes(&instance_snapshot, sizeof(instance_snapshot));
	scrub_bytes(&instance_check, sizeof(instance_check));
	scrub_bytes(&evidence_seed, sizeof(evidence_seed));
	scrub_bytes(&evidence_receipt, sizeof(evidence_receipt));
	return CB_SUCCESS;

fail:
	if (unwind) {
		expected = SMM_INVOCATION_LOADER_COMPOSITION_COMPOSING;
		if (!__atomic_compare_exchange_n(&composition->state, &expected,
			SMM_INVOCATION_LOADER_COMPOSITION_UNWINDING, false,
			__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			goto scrub_locals;
		if (evidence_receipt.acquired) {
			COMPOSITION_TEST_HOOK(100);
			(void)smm_invocation_evidence_loader_rollback(evidence,
				&evidence_receipt);
		}
		if (instance_published) {
			COMPOSITION_TEST_HOOK(101);
			smm_invocation_loader_instance_scrub(instance);
		}
		COMPOSITION_TEST_HOOK(102);
		smm_invocation_topology_scrub(topology);
		scrub_bytes((uint8_t *)composition + sizeof(composition->state),
			sizeof(*composition) - sizeof(composition->state));
		__atomic_store_n(&composition->state,
			SMM_INVOCATION_LOADER_COMPOSITION_FAILED, __ATOMIC_RELEASE);
		COMPOSITION_TEST_HOOK(103);
	}
scrub_locals:
	scrub_bytes(&topology_snapshot, sizeof(topology_snapshot));
	scrub_bytes(&topology_check, sizeof(topology_check));
	scrub_bytes(&instance_seed, sizeof(instance_seed));
	scrub_bytes(&instance_snapshot, sizeof(instance_snapshot));
	scrub_bytes(&instance_check, sizeof(instance_check));
	scrub_bytes(&evidence_seed, sizeof(evidence_seed));
	scrub_bytes(&evidence_receipt, sizeof(evidence_receipt));
	return CB_ERR;
}
