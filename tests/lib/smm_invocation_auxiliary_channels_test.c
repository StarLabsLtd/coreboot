/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/x86/smm_invocation_auxiliary_channels.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static struct smm_invocation_auxiliary_channels channels;
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_topology topology;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence primary;
static unsigned int provisions;
static unsigned int rollbacks;
static unsigned int fail_provision;
static unsigned int mutate_hook;

extern struct smm_invocation_evidence *
smm_invocation_auxiliary_channel_evidence(
	const volatile struct smm_invocation_auxiliary_channels *channels,
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *primary, uint32_t index);

void smm_invocation_auxiliary_channels_test_hook(uint32_t point)
{
	if (point != mutate_hook)
		return;
	if (point == 1U)
		topology.initial_apic_ids[0]++;
	else if (point == 2U)
		instance.loader_instance_nonce.low++;
	else if (point == 3U)
		composition.evidence_identity++;
	else if (point == 4U)
		channels.binding[1].evidence_identity++;
}

const struct smm_invocation_evidence *
smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *candidate,
	const struct smm_invocation_evidence *evidence)
{
	return candidate == &composition &&
		candidate->state == SMM_INVOCATION_LOADER_COMPOSITION_READY &&
		candidate->evidence_identity == (uintptr_t)evidence ? evidence : NULL;
}

enum cb_err smm_invocation_topology_read(
	const struct smm_invocation_topology *candidate,
	struct smm_invocation_topology *snapshot)
{
	if (candidate != &topology)
		return CB_ERR;
	*snapshot = *candidate;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *candidate,
	struct smm_invocation_loader_instance *snapshot)
{
	if (candidate != &instance)
		return CB_ERR;
	*snapshot = *candidate;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_loader_provision(
	struct smm_invocation_evidence *evidence,
	const struct smm_invocation_loader_seed *seed,
	struct smm_invocation_evidence_loader_receipt *receipt)
{
	provisions++;
	if (provisions == fail_provision)
		return CB_ERR;
	memset(evidence, 0, sizeof(*evidence));
	evidence->state = SMM_INVOCATION_READY;
	evidence->active_cpus = seed->active_cpus;
	evidence->bsp_cpu = seed->bsp_cpu;
	evidence->loader_instance_nonce = seed->loader_instance_nonce;
	evidence->loader_lifecycle = seed->lifecycle;
	evidence->expected_cpus = (1ULL << seed->active_cpus) - 1ULL;
	memcpy(evidence->participant_apic_ids, seed->participant_apic_ids,
		sizeof(evidence->participant_apic_ids));
	receipt->evidence_identity = (uintptr_t)evidence;
	receipt->acquired = 1U;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_evidence_loader_rollback(
	struct smm_invocation_evidence *evidence,
	struct smm_invocation_evidence_loader_receipt *receipt)
{
	rollbacks++;
	memset(evidence, 0, sizeof(*evidence));
	memset(receipt, 0, sizeof(*receipt));
	return CB_SUCCESS;
}

static void setup(void)
{
	memset(&channels, 0, sizeof(channels));
	memset(&composition, 0, sizeof(composition));
	memset(&topology, 0, sizeof(topology));
	memset(&instance, 0, sizeof(instance));
	memset(&primary, 0, sizeof(primary));
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.evidence_identity = (uintptr_t)&primary;
	topology.state = SMM_INVOCATION_TOPOLOGY_READY;
	topology.active_cpus = 2U;
	topology.initial_apic_ids[0] = 2U;
	topology.initial_apic_ids[1] = 4U;
	instance.state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance.loader_instance_nonce.low = 1U;
	instance.loader_instance_nonce.high = 2U;
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	provisions = 0U;
	rollbacks = 0U;
	fail_provision = 0U;
	mutate_hook = 0U;
}

static enum cb_err compose(void)
{
	return smm_invocation_auxiliary_channels_compose(&channels, &composition,
		&topology, &instance, &primary);
}

static struct smm_invocation_evidence *lookup(uint32_t index)
{
	return smm_invocation_auxiliary_channel_evidence(&channels, &composition,
		&topology, &instance, &primary, index);
}

static bool all_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return !value;
}

static void expect_compose_failure(void)
{
	assert(compose() == CB_ERR);
	assert(channels.state == SMM_INVOCATION_AUXILIARY_CHANNELS_FAILED);
	assert(all_zero((uint8_t *)&channels + sizeof(channels.state),
		sizeof(channels) - sizeof(channels.state)));
}

static void valid_and_distinct(void)
{
	assert(compose() == CB_SUCCESS);
	assert(lookup(0U) == &channels.evidence[0]);
	assert(lookup(1U) == &channels.evidence[1]);
	assert(lookup(0U) != lookup(1U));
	assert(!lookup(SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT));
	assert(!lookup(UINT32_MAX));
}

static void mutate_owner(const char *mode)
{
	valid_and_distinct();
	if (!strcmp(mode, "revision"))
		channels.revision++;
	else if (!strcmp(mode, "size"))
		channels.size--;
	else if (!strcmp(mode, "count"))
		channels.count--;
	else if (!strcmp(mode, "reserved"))
		channels.reserved[1]++;
	else if (!strcmp(mode, "composition-id"))
		channels.composition_identity++;
	else if (!strcmp(mode, "topology-id"))
		channels.topology_identity++;
	else if (!strcmp(mode, "instance-id"))
		channels.instance_identity++;
	else if (!strcmp(mode, "primary-id"))
		channels.primary_evidence_identity++;
	else if (!strcmp(mode, "seed"))
		channels.seed.participant_apic_ids[0]++;
	else if (!strcmp(mode, "index"))
		channels.binding[1].index = 0U;
	else if (!strcmp(mode, "binding-reserved"))
		channels.binding[0].reserved++;
	else if (!strcmp(mode, "identity"))
		channels.binding[1].evidence_identity++;
	else if (!strcmp(mode, "evidence-tail"))
		channels.evidence[1].participant_apic_ids[63]++;
	else if (!strcmp(mode, "evidence-mask"))
		channels.evidence[0].expected_cpus++;
	else if (!strcmp(mode, "evidence-nonce"))
		channels.evidence[1].loader_instance_nonce.high++;
	else if (!strcmp(mode, "evidence-lifecycle"))
		channels.evidence[0].loader_lifecycle++;
	else if (!strcmp(mode, "evidence-empty"))
		channels.evidence[0].state = SMM_INVOCATION_EMPTY;
	else if (!strcmp(mode, "evidence-provisioning"))
		channels.evidence[0].state = SMM_INVOCATION_PROVISIONING;
	else
		abort();
	assert(!lookup(0U));
}

int main(int argc, char **argv)
{
	struct smm_invocation_auxiliary_channels saved;
	_Alignas(8) uint8_t misaligned[sizeof(channels) + 1U];
	const char *mode;

	assert(argc == 2);
	mode = argv[1];
	setup();
	if (!strcmp(mode, "valid")) {
		valid_and_distinct();
	} else if (!strcmp(mode, "first-failure") ||
		   !strcmp(mode, "second-failure")) {
		fail_provision = !strcmp(mode, "first-failure") ? 1U : 2U;
		expect_compose_failure();
		assert(rollbacks == fail_provision - 1U);
		assert(all_zero(channels.evidence, sizeof(channels.evidence)));
	} else if (!strncmp(mode, "hook", 4U)) {
		mutate_hook = (unsigned int)(mode[4] - '0');
		if (mutate_hook == 4U) {
			assert(compose() == CB_SUCCESS);
			assert(!lookup(0U));
		} else {
			expect_compose_failure();
			assert(rollbacks == 2U);
		}
	} else if (!strcmp(mode, "dirty")) {
		channels.revision = 1U;
		expect_compose_failure();
	} else if (!strcmp(mode, "double")) {
		assert(compose() == CB_SUCCESS);
		saved = channels;
		assert(compose() == CB_ERR);
		assert(!memcmp(&saved, &channels, sizeof(saved)));
	} else if (!strcmp(mode, "concurrent")) {
		channels.state = SMM_INVOCATION_AUXILIARY_CHANNELS_PROVISIONING;
		saved = channels;
		assert(compose() == CB_ERR);
		assert(!memcmp(&saved, &channels, sizeof(saved)));
	} else if (!strcmp(mode, "null")) {
		assert(smm_invocation_auxiliary_channels_compose(NULL, &composition,
			&topology, &instance, &primary) == CB_ERR);
	} else if (!strcmp(mode, "misaligned")) {
		assert(smm_invocation_auxiliary_channels_compose((void *)(misaligned + 1U),
			&composition, &topology, &instance, &primary) == CB_ERR);
	} else if (!strcmp(mode, "overlap")) {
		assert(smm_invocation_auxiliary_channels_compose(&channels,
			(void *)&channels, &topology, &instance, &primary) == CB_ERR);
	} else if (!strcmp(mode, "lookup-alias")) {
		assert(compose() == CB_SUCCESS);
		assert(!smm_invocation_auxiliary_channel_evidence(&channels,
			(void *)&channels, &topology, &instance, &primary, 0U));
	} else if (!strcmp(mode, "abort")) {
		assert(compose() == CB_SUCCESS);
		smm_invocation_auxiliary_channels_loader_abort(&channels);
		assert(channels.state == SMM_INVOCATION_AUXILIARY_CHANNELS_FAILED);
		assert(all_zero((uint8_t *)&channels + sizeof(channels.state),
			sizeof(channels) - sizeof(channels.state)));
		assert(!lookup(0U));
	} else if (!strcmp(mode, "abort-invalid")) {
		smm_invocation_auxiliary_channels_loader_abort(NULL);
		smm_invocation_auxiliary_channels_loader_abort((void *)(misaligned + 1U));
		smm_invocation_auxiliary_channels_loader_abort((void *)(UINTPTR_MAX - 7U));
	} else if (!strcmp(mode, "composition-drift")) {
		valid_and_distinct();
		composition.evidence_identity++;
		assert(!lookup(0U));
	} else if (!strcmp(mode, "topology-drift")) {
		valid_and_distinct();
		topology.initial_apic_ids[0]++;
		assert(!lookup(0U));
	} else if (!strcmp(mode, "instance-drift")) {
		valid_and_distinct();
		instance.loader_instance_nonce.low++;
		assert(!lookup(0U));
	} else if (!strcmp(mode, "poisoned-phase")) {
		valid_and_distinct();
		channels.evidence[0].state = SMM_INVOCATION_POISONED;
		assert(lookup(0U) == &channels.evidence[0]);
	} else {
		mutate_owner(mode);
	}
	return 0;
}
