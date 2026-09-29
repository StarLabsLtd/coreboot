/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm_invocation_auxiliary_channels.h>
#include "smm_invocation_auxiliary_channels_private.h"
#include <commonlib/helpers.h>
#include <string.h>

#if defined(__TEST__)
void smm_invocation_auxiliary_channels_test_hook(uint32_t point);
#define AUX_TEST_HOOK(point) smm_invocation_auxiliary_channels_test_hook(point)
#else
#define AUX_TEST_HOOK(point) do { } while (0)
#endif

/* Keep authority-bearing copies in explicit, audited SMM stack boundaries. */
static __noinline void auxiliary_scrub(void *buffer, size_t size)
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
	return !value;
}

static bool range_valid(const void *base, size_t size, size_t alignment)
{
	const uintptr_t address = (uintptr_t)base;

	return base && size && !(address % alignment) &&
		address <= UINTPTR_MAX - (size - 1U);
}

static bool overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t a = (uintptr_t)first;
	const uintptr_t b = (uintptr_t)second;

	return a <= b ? b - a < first_size : a - b < second_size;
}

/* Keep seed construction an explicit audited SMM callgraph boundary. */
static __noinline void auxiliary_seed_build(struct smm_invocation_loader_seed *seed,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance)
{
	memset(seed, 0, sizeof(*seed));
	seed->revision = SMM_INVOCATION_EVIDENCE_REVISION;
	seed->size = sizeof(*seed);
	seed->active_cpus = topology->active_cpus;
	seed->bsp_cpu = topology->bsp_cpu;
	seed->loader_instance_nonce = instance->loader_instance_nonce;
	seed->lifecycle = instance->lifecycle;
	memcpy(seed->participant_apic_ids, topology->initial_apic_ids,
		topology->active_cpus * sizeof(seed->participant_apic_ids[0]));
}

static bool evidence_bound(const volatile struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_seed *seed)
{
	const uint32_t state = __atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);

	if (!seed->active_cpus || seed->active_cpus > 64U)
		return false;
	const uint64_t expected_cpus = seed->active_cpus == 64U ? UINT64_MAX :
		(1ULL << seed->active_cpus) - 1ULL;
	const bool bound = evidence->active_cpus == seed->active_cpus &&
		evidence->bsp_cpu == seed->bsp_cpu &&
		evidence->expected_cpus == expected_cpus &&
		smm_invocation_loader_instance_nonce_equal(
			evidence->loader_instance_nonce,
			seed->loader_instance_nonce) &&
		evidence->loader_lifecycle == seed->lifecycle &&
		!memcmp((const void *)(uintptr_t)evidence->participant_apic_ids,
			seed->participant_apic_ids,
			sizeof(seed->participant_apic_ids));

	return state >= SMM_INVOCATION_READY &&
		state <= SMM_INVOCATION_POISONED && bound && state ==
		__atomic_load_n(&evidence->state, __ATOMIC_ACQUIRE);
}

static bool inputs_valid(struct smm_invocation_auxiliary_channels *channels,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *primary)
{
	const void *objects[] = { channels, composition, topology, instance,
		primary };
	const size_t sizes[] = { sizeof(*channels), sizeof(*composition),
		sizeof(*topology), sizeof(*instance), sizeof(*primary) };

	for (size_t i = 0; i < ARRAY_SIZE(objects); i++) {
		if (!range_valid(objects[i], sizes[i], 8U))
			return false;
		for (size_t j = i + 1U; j < ARRAY_SIZE(objects); j++)
			if (overlap(objects[i], sizes[i], objects[j], sizes[j]))
				return false;
	}
	return true;
}

enum cb_err smm_invocation_auxiliary_channels_compose(
	struct smm_invocation_auxiliary_channels *channels,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *primary)
{
	struct smm_invocation_loader_seed seed = { 0 };
	struct smm_invocation_topology topology_snapshot = { 0 };
	struct smm_invocation_topology topology_check = { 0 };
	struct smm_invocation_loader_instance instance_snapshot = { 0 };
	struct smm_invocation_loader_instance instance_check = { 0 };
	struct smm_invocation_evidence_loader_receipt
		receipts[SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT] = { 0 };
	uint32_t expected = SMM_INVOCATION_AUXILIARY_CHANNELS_EMPTY;

	if (!inputs_valid(channels, composition, topology, instance, primary) ||
	    !__atomic_compare_exchange_n(&channels->state, &expected,
		SMM_INVOCATION_AUXILIARY_CHANNELS_PROVISIONING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (!zero((uint8_t *)channels + sizeof(channels->state),
		sizeof(*channels) - sizeof(channels->state)) ||
	    smm_invocation_loader_composition_evidence(composition, primary) !=
		primary ||
	    smm_invocation_topology_read(topology, &topology_snapshot) != CB_SUCCESS ||
	    smm_invocation_loader_instance_read(instance, &instance_snapshot) !=
		CB_SUCCESS)
		goto fail;
	auxiliary_seed_build(&seed, &topology_snapshot, &instance_snapshot);
	for (size_t i = 0; i < SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT; i++) {
		if (smm_invocation_evidence_loader_provision(&channels->evidence[i], &seed,
			&receipts[i]) != CB_SUCCESS ||
		    !evidence_bound(&channels->evidence[i], &seed))
			goto fail;
		AUX_TEST_HOOK(1U + (uint32_t)i);
	}
	channels->revision = SMM_INVOCATION_AUXILIARY_CHANNELS_REVISION;
	channels->size = sizeof(*channels);
	channels->count = SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT;
	channels->composition_identity = (uintptr_t)composition;
	channels->topology_identity = (uintptr_t)topology;
	channels->instance_identity = (uintptr_t)instance;
	channels->primary_evidence_identity = (uintptr_t)primary;
	channels->seed = seed;
	for (size_t i = 0; i < SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT; i++) {
		channels->binding[i].index = (uint32_t)i;
		channels->binding[i].evidence_identity =
			(uintptr_t)&channels->evidence[i];
	}
	AUX_TEST_HOOK(3);
	if (smm_invocation_loader_composition_evidence(composition, primary) !=
		primary ||
	    smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS ||
	    memcmp(&topology_snapshot, &topology_check, sizeof(topology_snapshot)) ||
	    smm_invocation_loader_instance_read(instance, &instance_check) !=
		CB_SUCCESS ||
	    memcmp(&instance_snapshot, &instance_check, sizeof(instance_snapshot)))
		goto fail;
	for (size_t i = 0; i < SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT; i++)
		if (!evidence_bound(&channels->evidence[i],
			&channels->seed))
			goto fail;
	auxiliary_scrub(&topology_check, sizeof(topology_check));
	auxiliary_scrub(&instance_check, sizeof(instance_check));
	if (channels->reserved[0] || channels->reserved[1] ||
	    smm_invocation_loader_composition_evidence(composition, primary) !=
		primary ||
	    smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS ||
	    memcmp(&topology_snapshot, &topology_check, sizeof(topology_snapshot)) ||
	    smm_invocation_loader_instance_read(instance, &instance_check) !=
		CB_SUCCESS ||
	    memcmp(&instance_snapshot, &instance_check, sizeof(instance_snapshot)))
		goto fail;
	expected = SMM_INVOCATION_AUXILIARY_CHANNELS_PROVISIONING;
	if (!__atomic_compare_exchange_n(&channels->state, &expected,
		SMM_INVOCATION_AUXILIARY_CHANNELS_READY, false, __ATOMIC_RELEASE,
		__ATOMIC_ACQUIRE))
		goto fail;
	auxiliary_scrub(&seed, sizeof(seed));
	auxiliary_scrub(&topology_snapshot, sizeof(topology_snapshot));
	auxiliary_scrub(&topology_check, sizeof(topology_check));
	auxiliary_scrub(&instance_snapshot, sizeof(instance_snapshot));
	auxiliary_scrub(&instance_check, sizeof(instance_check));
	auxiliary_scrub(receipts, sizeof(receipts));
	return CB_SUCCESS;
fail:
	for (size_t i = 0; i < SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT; i++)
		if (receipts[i].acquired)
			(void)smm_invocation_evidence_loader_rollback(
				&channels->evidence[i],
				&receipts[i]);
	auxiliary_scrub((uint8_t *)channels + sizeof(channels->state),
		sizeof(*channels) - sizeof(channels->state));
	__atomic_store_n(&channels->state,
		SMM_INVOCATION_AUXILIARY_CHANNELS_FAILED, __ATOMIC_RELEASE);
	auxiliary_scrub(&seed, sizeof(seed));
	auxiliary_scrub(&topology_snapshot, sizeof(topology_snapshot));
	auxiliary_scrub(&topology_check, sizeof(topology_check));
	auxiliary_scrub(&instance_snapshot, sizeof(instance_snapshot));
	auxiliary_scrub(&instance_check, sizeof(instance_check));
	auxiliary_scrub(receipts, sizeof(receipts));
	return CB_ERR;
}

void smm_invocation_auxiliary_channels_loader_abort(
	struct smm_invocation_auxiliary_channels *channels)
{
	if (range_valid(channels, sizeof(*channels), _Alignof(*channels))) {
		auxiliary_scrub(channels, sizeof(*channels));
		__atomic_store_n(&channels->state,
			SMM_INVOCATION_AUXILIARY_CHANNELS_FAILED, __ATOMIC_RELEASE);
	}
}

#if ENV_SMM || ENV_TEST
struct smm_invocation_evidence *smm_invocation_auxiliary_channel_evidence(
	const volatile struct smm_invocation_auxiliary_channels *channels,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *primary, uint32_t index)
{
	struct smm_invocation_auxiliary_channels *mutable_channels =
		(void *)(uintptr_t)channels;
	struct smm_invocation_topology topology_snapshot = { 0 };
	struct smm_invocation_topology topology_check = { 0 };
	struct smm_invocation_loader_instance instance_snapshot = { 0 };
	struct smm_invocation_loader_instance instance_check = { 0 };
	struct smm_invocation_loader_seed expected_seed = { 0 };
	uint8_t metadata[offsetof(struct smm_invocation_auxiliary_channels,
		evidence) - sizeof(channels->state)];

	if (!channels || !composition || !topology || !instance || !primary ||
	    index >= SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT ||
	    !inputs_valid(mutable_channels, composition, topology, instance,
		primary))
		return NULL;
	if (__atomic_load_n(&channels->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_AUXILIARY_CHANNELS_READY)
		return NULL;
	memcpy(metadata, (const uint8_t *)(uintptr_t)channels +
		sizeof(channels->state),
		sizeof(metadata));
	AUX_TEST_HOOK(4);
	if (__atomic_load_n(&channels->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_AUXILIARY_CHANNELS_READY ||
	    channels->revision != SMM_INVOCATION_AUXILIARY_CHANNELS_REVISION ||
	    channels->size != sizeof(*channels) ||
	    channels->count != SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT ||
	    channels->reserved[0] || channels->reserved[1] ||
	    channels->composition_identity != (uintptr_t)composition ||
	    channels->topology_identity != (uintptr_t)topology ||
	    channels->instance_identity != (uintptr_t)instance ||
	    channels->primary_evidence_identity != (uintptr_t)primary ||
	    channels->binding[0].index != 0 || channels->binding[0].reserved ||
	    channels->binding[1].index != 1 || channels->binding[1].reserved ||
	    channels->binding[0].evidence_identity !=
		(uintptr_t)&channels->evidence[0] ||
	    channels->binding[1].evidence_identity !=
		(uintptr_t)&channels->evidence[1] ||
	    smm_invocation_loader_composition_evidence(composition, primary) !=
		primary ||
	    smm_invocation_topology_read(topology, &topology_snapshot) != CB_SUCCESS ||
	    smm_invocation_loader_instance_read(instance, &instance_snapshot) !=
		CB_SUCCESS)
		goto fail;
	auxiliary_seed_build(&expected_seed, &topology_snapshot, &instance_snapshot);
	if (memcmp((const void *)(uintptr_t)&channels->seed, &expected_seed,
		sizeof(expected_seed)) ||
	    !evidence_bound(&channels->evidence[0], &expected_seed) ||
	    !evidence_bound(&channels->evidence[1], &expected_seed) ||
	    memcmp(metadata, (const uint8_t *)(uintptr_t)channels +
		sizeof(channels->state),
		sizeof(metadata)) ||
	    smm_invocation_loader_composition_evidence(composition, primary) !=
		primary ||
	    smm_invocation_topology_read(topology, &topology_check) != CB_SUCCESS ||
	    memcmp(&topology_snapshot, &topology_check, sizeof(topology_snapshot)) ||
	    smm_invocation_loader_instance_read(instance, &instance_check) !=
		CB_SUCCESS ||
	    memcmp(&instance_snapshot, &instance_check, sizeof(instance_snapshot)) ||
	    __atomic_load_n(&channels->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_AUXILIARY_CHANNELS_READY)
		goto fail;
	auxiliary_scrub(metadata, sizeof(metadata));
	auxiliary_scrub(&expected_seed, sizeof(expected_seed));
	auxiliary_scrub(&topology_snapshot, sizeof(topology_snapshot));
	auxiliary_scrub(&topology_check, sizeof(topology_check));
	auxiliary_scrub(&instance_snapshot, sizeof(instance_snapshot));
	auxiliary_scrub(&instance_check, sizeof(instance_check));
	return &mutable_channels->evidence[index];
fail:
	auxiliary_scrub(metadata, sizeof(metadata));
	auxiliary_scrub(&expected_seed, sizeof(expected_seed));
	auxiliary_scrub(&topology_snapshot, sizeof(topology_snapshot));
	auxiliary_scrub(&topology_check, sizeof(topology_check));
	auxiliary_scrub(&instance_snapshot, sizeof(instance_snapshot));
	auxiliary_scrub(&instance_check, sizeof(instance_check));
	return NULL;
}
#endif
