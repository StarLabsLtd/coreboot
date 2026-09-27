/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <commonlib/helpers.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define TEST_CPUS 4U

struct fixture {
	struct smm_invocation_loader_composition composition;
	struct smm_invocation_topology topology;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_evidence evidence;
};

static struct fixture *active_fixture;
static uint32_t provider_calls;
static uint32_t provider_fail;
static uint32_t provider_malformed;
static uint32_t hook_point;
static uint32_t hook_entered;
static uint32_t hook_release;
static uint32_t hook_action;
static enum cb_err reentry_result;
static uint32_t unwind_trace[4];
static uint32_t unwind_count;
static uint32_t forward_trace[7];
static uint32_t forward_count;
static bool forward_trace_enabled;
static uint32_t evidence_hook_point;
static uint32_t evidence_hook_action;
static uint32_t evidence_close_trace;
static struct smm_invocation_evidence foreign_replacement;

static void assert_failed(const struct fixture *fixture);

void smm_invocation_topology_test_hook(uint32_t point)
{
	(void)point;
}

void smm_invocation_loader_instance_test_hook(uint32_t point)
{
	(void)point;
}

void smm_invocation_evidence_test_hook(uint32_t point)
{
	if (point >= 48U && point <= 50U)
		evidence_close_trace |= 1U << (point - 48U);
	if (point != evidence_hook_point)
		return;
	evidence_hook_point = 0;
	if (evidence_hook_action == 1)
		__atomic_fetch_or(&active_fixture->evidence.state, 1U << 9,
			__ATOMIC_ACQ_REL);
	else if (evidence_hook_action == 2)
		active_fixture->evidence.reserved = 1;
	else if (evidence_hook_action == 3) {
		active_fixture->evidence.active_cpus--;
		evidence_hook_point = 47;
		evidence_hook_action = 1;
	}
}

enum cb_err smm_invocation_platform_loader_instance_take(
	struct smm_invocation_loader_instance_seed *seed)
{
	provider_calls++;
	memset(seed, 0, sizeof(*seed));
	if (provider_fail)
		return CB_ERR;
	*seed = (struct smm_invocation_loader_instance_seed) {
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(*seed),
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
		.loader_instance_nonce = {
			.low = 0x1122334455667788ULL,
			.high = 0x8877665544332211ULL,
		},
	};
	switch (provider_malformed) {
	case 1:
		seed->revision++;
		break;
	case 2:
		seed->size--;
		break;
	case 3:
		seed->lifecycle = 0;
		break;
	case 4:
		seed->loader_instance_nonce.low = 0;
		seed->loader_instance_nonce.high = 0;
		break;
	case 5:
		seed->reserved = 1;
		break;
	}
	return CB_SUCCESS;
}

void smm_invocation_loader_composition_test_hook(uint32_t point)
{
	if (forward_trace_enabled && point >= 1U && point <= 7U &&
	    forward_count < ARRAY_SIZE(forward_trace))
		forward_trace[forward_count++] = point;
	if (point >= 100U && unwind_count < 4U)
		unwind_trace[unwind_count++] = point;
	if (point != hook_point)
		return;
	hook_point = 0;
	switch (hook_action) {
	case 1:
		reentry_result = smm_invocation_loader_compose(
			&active_fixture->composition, &active_fixture->topology,
			&active_fixture->instance, &active_fixture->evidence);
		break;
	case 2:
		active_fixture->topology.revision++;
		break;
	case 3:
		active_fixture->instance.reserved = 1;
		break;
	case 4:
		active_fixture->evidence.reserved = 1;
		break;
	case 5:
		active_fixture->topology.initial_apic_ids[1]++;
		break;
	case 6:
		__atomic_store_n(&active_fixture->composition.state,
			SMM_INVOCATION_LOADER_COMPOSITION_READY, __ATOMIC_RELEASE);
		break;
	case 7:
		__atomic_store_n(&hook_entered, 1U, __ATOMIC_RELEASE);
		while (!__atomic_load_n(&hook_release, __ATOMIC_ACQUIRE))
			__asm__ volatile ("pause");
		break;
	case 8:
		active_fixture->topology.state = SMM_INVOCATION_TOPOLOGY_TAKING;
		break;
	case 9:
		active_fixture->topology.size--;
		break;
	case 10:
		active_fixture->topology.active_cpus--;
		break;
	case 11:
		active_fixture->topology.bsp_cpu = 1;
		break;
	case 12:
		active_fixture->topology.reserved[1] = 1;
		break;
	case 13:
		active_fixture->topology.initial_apic_ids[TEST_CPUS] = 1;
		break;
	case 14:
		active_fixture->instance.state =
			SMM_INVOCATION_LOADER_INSTANCE_PROVISIONING;
		break;
	case 15:
		active_fixture->instance.revision++;
		break;
	case 16:
		active_fixture->instance.size--;
		break;
	case 17:
		active_fixture->instance.lifecycle = 0;
		break;
	case 18:
		active_fixture->instance.loader_instance_nonce.low++;
		break;
	case 19:
		active_fixture->instance.loader_instance_nonce.high++;
		break;
	case 20:
		active_fixture->composition.owner_attempt++;
		break;
	case 21:
		active_fixture->composition.reserved[0] = 1;
		break;
	case 22:
		active_fixture->evidence.state = SMM_INVOCATION_CLOSED;
		foreign_replacement = active_fixture->evidence;
		break;
	case 23:
		active_fixture->evidence.active_cpus--;
		break;
	case 24:
		active_fixture->evidence.bsp_cpu = 1;
		break;
	case 25:
		active_fixture->evidence.loader_instance_nonce.low++;
		break;
	case 26:
		active_fixture->evidence.loader_instance_nonce.high++;
		break;
	case 27:
		active_fixture->evidence.loader_lifecycle = 0;
		break;
	case 28:
		active_fixture->evidence.participant_apic_ids[1]++;
		break;
	case 29:
		active_fixture->evidence.participant_apic_ids[TEST_CPUS] = 1;
		break;
	case 30:
		active_fixture->evidence.expected_cpus = 0;
		break;
	case 31:
		active_fixture->composition.evidence_identity++;
		break;
	case 32:
		active_fixture->topology.revision++;
		hook_point = 5;
		hook_action = 33;
		break;
	case 33:
		active_fixture->topology.revision--;
		break;
	case 34:
		active_fixture->topology.revision++;
		hook_point = 7;
		hook_action = 33;
		break;
	case 35:
		active_fixture->instance.revision++;
		hook_point = 5;
		hook_action = 36;
		break;
	case 36:
		active_fixture->instance.revision--;
		break;
	case 37:
		active_fixture->instance.revision++;
		hook_point = 7;
		hook_action = 36;
		break;
	}
}

static void setup(struct fixture *fixture)
{
	struct smm_invocation_topology_builder builder;
	uint32_t installed[TEST_CPUS] = { 10, 11, 12, 13 };

	memset(fixture, 0, sizeof(*fixture));
	assert(smm_invocation_topology_begin(&builder, &fixture->topology,
		TEST_CPUS, installed[0]) == CB_SUCCESS);
	for (uint32_t cpu = 0; cpu < TEST_CPUS; cpu++)
		assert(smm_invocation_topology_append(&builder, &installed[cpu],
			installed[cpu]) == CB_SUCCESS);
	assert(smm_invocation_topology_publish(&builder, installed, TEST_CPUS,
		TEST_CPUS) == CB_SUCCESS);
	active_fixture = fixture;
	provider_calls = 0;
	provider_fail = 0;
	provider_malformed = 0;
	hook_point = 0;
	hook_action = 0;
	hook_entered = 0;
	hook_release = 0;
	reentry_result = CB_SUCCESS;
	memset(unwind_trace, 0, sizeof(unwind_trace));
	unwind_count = 0;
	memset(forward_trace, 0, sizeof(forward_trace));
	forward_count = 0;
	forward_trace_enabled = false;
	evidence_hook_point = 0;
	evidence_hook_action = 0;
	evidence_close_trace = 0;
	memset(&foreign_replacement, 0, sizeof(foreign_replacement));
}

static void test_owned_evidence_failures(void)
{
	struct fixture fixture;
	const struct {
		uint32_t point;
		uint32_t action;
	} failures[] = {
		{ 36, 1 }, { 37, 1 }, { 38, 1 }, { 39, 1 }, { 44, 1 },
		{ 44, 2 }, { 44, 3 }, { 45, 1 }, { 46, 1 },
	};

	for (size_t index = 0; index < ARRAY_SIZE(failures); index++) {
		setup(&fixture);
		evidence_hook_point = failures[index].point;
		evidence_hook_action = failures[index].action;
		assert(smm_invocation_loader_compose(&fixture.composition,
			&fixture.topology, &fixture.instance,
			&fixture.evidence) == CB_ERR);
		assert_failed(&fixture);
		assert(unwind_trace[0] == 100U);
		if (failures[index].point == 36U)
			assert(evidence_close_trace == 1U);
		else if (failures[index].point == 39U)
			assert(evidence_close_trace == 2U);
		else if (failures[index].point == 44U &&
			 failures[index].action == 1U)
			assert(evidence_close_trace == 4U);
	}
}

static void assert_ready(const struct fixture *fixture)
{
	const struct smm_invocation_topology topology = fixture->topology;

	assert(fixture->composition.state ==
		SMM_INVOCATION_LOADER_COMPOSITION_READY);
	assert(fixture->composition.owner_attempt == 1U);
	assert(!fixture->composition.reserved[0] &&
		!fixture->composition.reserved[1]);
	assert(fixture->composition.evidence_identity ==
		(uint64_t)(uintptr_t)&fixture->evidence);
	assert(fixture->topology.state == SMM_INVOCATION_TOPOLOGY_READY);
	assert(fixture->instance.state == SMM_INVOCATION_LOADER_INSTANCE_READY);
	assert(fixture->instance.revision ==
		SMM_INVOCATION_LOADER_INSTANCE_REVISION);
	assert(fixture->instance.size == sizeof(fixture->instance));
	assert(fixture->instance.lifecycle ==
		SMM_INVOCATION_LOADER_NON_S3_LOAD);
	assert(fixture->instance.loader_instance_nonce.low ==
		0x1122334455667788ULL);
	assert(fixture->instance.loader_instance_nonce.high ==
		0x8877665544332211ULL);
	assert(!fixture->instance.reserved);
	assert(fixture->evidence.state == SMM_INVOCATION_READY);
	assert(fixture->evidence.active_cpus == TEST_CPUS);
	assert(fixture->evidence.bsp_cpu == 0U);
	assert(!memcmp(fixture->evidence.participant_apic_ids,
		topology.initial_apic_ids,
		sizeof(fixture->evidence.participant_apic_ids)));
	assert(fixture->evidence.loader_instance_nonce.low ==
		fixture->instance.loader_instance_nonce.low);
	assert(fixture->evidence.loader_instance_nonce.high ==
		fixture->instance.loader_instance_nonce.high);
	assert(fixture->evidence.loader_lifecycle == fixture->instance.lifecycle);
	assert(fixture->evidence.expected_cpus == 0xfU);
	assert(smm_invocation_loader_composition_evidence(
		&fixture->composition, &fixture->evidence) == &fixture->evidence);
}

static void assert_failed(const struct fixture *fixture)
{
	const struct smm_invocation_topology empty_topology = { 0 };
	const struct smm_invocation_loader_instance empty_instance = { 0 };
	const struct smm_invocation_evidence empty_evidence = { 0 };

	assert(fixture->composition.state ==
		SMM_INVOCATION_LOADER_COMPOSITION_FAILED);
	assert(fixture->composition.owner_attempt == 0U);
	assert(!fixture->composition.reserved[0] &&
		!fixture->composition.reserved[1]);
	assert(!fixture->composition.evidence_identity);
	assert(!memcmp(&fixture->topology, &empty_topology,
		sizeof(empty_topology)));
	assert(!memcmp(&fixture->instance, &empty_instance,
		sizeof(empty_instance)));
	assert(!memcmp(&fixture->evidence, &empty_evidence,
		sizeof(empty_evidence)));
	assert(!smm_invocation_loader_composition_evidence(
		&fixture->composition, &fixture->evidence));
}

static struct smm_invocation_loader_seed direct_seed(void)
{
	return (struct smm_invocation_loader_seed) {
		.revision = SMM_INVOCATION_EVIDENCE_REVISION,
		.size = sizeof(struct smm_invocation_loader_seed),
		.active_cpus = 1,
		.bsp_cpu = 0,
		.loader_instance_nonce = { .low = 4, .high = 5 },
		.participant_apic_ids = { 10 },
		.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD,
	};
}

static void test_evidence_receipt(void)
{
	struct smm_invocation_evidence evidence = { 0 };
	struct smm_invocation_evidence before;
	struct smm_invocation_evidence_loader_receipt receipt;
	struct smm_invocation_loader_seed seed = direct_seed();
	uint8_t unaligned[sizeof(receipt) + 1U] __aligned(8);

	assert(smm_invocation_evidence_loader_provision(&evidence, &seed,
		&receipt) == CB_SUCCESS);
	before = evidence;
	receipt.evidence_identity++;
	assert(smm_invocation_evidence_loader_rollback(&evidence, &receipt) ==
		CB_ERR);
	assert(!memcmp(&evidence, &before, sizeof(before)));
	assert(!memcmp(&receipt,
		&(const struct smm_invocation_evidence_loader_receipt) { 0 },
		sizeof(receipt)));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_loader_provision(&evidence, &seed,
		&receipt) == CB_SUCCESS);
	receipt.terminal_state = SMM_INVOCATION_CLOSED;
	before = evidence;
	assert(smm_invocation_evidence_loader_rollback(&evidence, &receipt) ==
		CB_ERR);
	assert(!memcmp(&evidence, &before, sizeof(before)));
	assert(!memcmp(&receipt,
		&(const struct smm_invocation_evidence_loader_receipt) { 0 },
		sizeof(receipt)));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_loader_provision(&evidence, &seed,
		&receipt) == CB_SUCCESS);
	assert(smm_invocation_evidence_loader_rollback(&evidence, &receipt) ==
		CB_SUCCESS);
	assert(!memcmp(&evidence,
		&(const struct smm_invocation_evidence) { 0 }, sizeof(evidence)));
	assert(!memcmp(&receipt,
		&(const struct smm_invocation_evidence_loader_receipt) { 0 },
		sizeof(receipt)));
	assert(smm_invocation_evidence_loader_rollback(&evidence, &receipt) ==
		CB_ERR);

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_loader_provision(&evidence, &seed,
		&receipt) == CB_SUCCESS);
	receipt.acquired = 2U;
	before = evidence;
	assert(smm_invocation_evidence_loader_rollback(&evidence, &receipt) ==
		CB_ERR);
	assert(!memcmp(&evidence, &before, sizeof(before)));
	assert(!memcmp(&receipt,
		&(const struct smm_invocation_evidence_loader_receipt) { 0 },
		sizeof(receipt)));

	memset(&evidence, 0, sizeof(evidence));
	assert(smm_invocation_evidence_loader_provision(&evidence, &seed,
		&receipt) == CB_SUCCESS);
	receipt.reserved = 1U;
	before = evidence;
	assert(smm_invocation_evidence_loader_rollback(&evidence, &receipt) ==
		CB_ERR);
	assert(!memcmp(&evidence, &before, sizeof(before)));
	assert(!memcmp(&receipt,
		&(const struct smm_invocation_evidence_loader_receipt) { 0 },
		sizeof(receipt)));

	before = evidence;
	assert(smm_invocation_evidence_loader_rollback(&evidence, NULL) ==
		CB_ERR_ARG);
	assert(!memcmp(&evidence, &before, sizeof(before)));
	assert(smm_invocation_evidence_loader_rollback(&evidence,
		(struct smm_invocation_evidence_loader_receipt *)&unaligned[1]) ==
		CB_ERR_ARG);
	assert(!memcmp(&evidence, &before, sizeof(before)));
	assert(smm_invocation_evidence_loader_rollback(&evidence,
		(struct smm_invocation_evidence_loader_receipt *)&evidence) ==
		CB_ERR_ARG);
	assert(!memcmp(&evidence, &before, sizeof(before)));

	assert(smm_invocation_evidence_loader_provision(&evidence, &seed,
		NULL) == CB_ERR_ARG);
	assert(!memcmp(&evidence, &before, sizeof(before)));
	assert(smm_invocation_evidence_loader_provision(&evidence, &seed,
		(struct smm_invocation_evidence_loader_receipt *)&evidence) ==
		CB_ERR_ARG);
	assert(!memcmp(&evidence, &before, sizeof(before)));
}

static void test_success_and_reentry(void)
{
	struct fixture fixture;

	setup(&fixture);
	hook_point = 4;
	hook_action = 1;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_SUCCESS);
	assert(reentry_result == CB_ERR);
	assert(provider_calls == 1U);
	assert_ready(&fixture);
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert(provider_calls == 1U);
	assert_ready(&fixture);
}

static void test_forward_order(void)
{
	struct fixture fixture;

	setup(&fixture);
	forward_trace_enabled = true;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_SUCCESS);
	assert(forward_count == ARRAY_SIZE(forward_trace));
	for (uint32_t index = 0; index < ARRAY_SIZE(forward_trace); index++)
		assert(forward_trace[index] == index + 1U);
	assert_ready(&fixture);
}

static void test_accessor_gate(void)
{
	struct fixture fixture;
	struct smm_invocation_loader_composition saved;
	struct smm_invocation_evidence other;

	setup(&fixture);
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_SUCCESS);
	other = fixture.evidence;
	assert(!smm_invocation_loader_composition_evidence(
		&fixture.composition, &other));
	__atomic_store_n(&fixture.evidence.state, SMM_INVOCATION_COLLECTING,
		__ATOMIC_RELEASE);
	assert(smm_invocation_loader_composition_evidence(
		&fixture.composition, &fixture.evidence) == &fixture.evidence);
	saved = fixture.composition;
	for (uint32_t state = SMM_INVOCATION_LOADER_COMPOSITION_EMPTY;
	     state <= SMM_INVOCATION_LOADER_COMPOSITION_FAILED; state++) {
		if (state == SMM_INVOCATION_LOADER_COMPOSITION_READY)
			continue;
		fixture.composition = saved;
		__atomic_store_n(&fixture.composition.state, state, __ATOMIC_RELEASE);
		assert(!smm_invocation_loader_composition_evidence(
			&fixture.composition, &fixture.evidence));
	}
	fixture.composition = saved;
	fixture.composition.owner_attempt++;
	assert(!smm_invocation_loader_composition_evidence(
		&fixture.composition, &fixture.evidence));
	fixture.composition = saved;
	fixture.composition.reserved[1] = 1U;
	assert(!smm_invocation_loader_composition_evidence(
		&fixture.composition, &fixture.evidence));
	fixture.composition = saved;
	fixture.composition.evidence_identity++;
	assert(!smm_invocation_loader_composition_evidence(
		&fixture.composition, &fixture.evidence));
	fixture.composition = saved;
	assert(!smm_invocation_loader_composition_evidence(NULL,
		&fixture.evidence));
	assert(!smm_invocation_loader_composition_evidence(
		&fixture.composition, NULL));
	assert(!smm_invocation_loader_composition_evidence(
		(const struct smm_invocation_loader_composition *)
		((const uint8_t *)&fixture.composition + 1U), &fixture.evidence));
	assert(!smm_invocation_loader_composition_evidence(
		&fixture.composition,
		(const struct smm_invocation_evidence *)&fixture.composition));
}

static void test_foreign_evidence_replacement(void)
{
	struct fixture fixture;

	setup(&fixture);
	hook_point = 7;
	hook_action = 22;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert(fixture.composition.state ==
		SMM_INVOCATION_LOADER_COMPOSITION_FAILED);
	assert(!memcmp(&fixture.topology,
		&(const struct smm_invocation_topology) { 0 },
		sizeof(fixture.topology)));
	assert(!memcmp(&fixture.instance,
		&(const struct smm_invocation_loader_instance) { 0 },
		sizeof(fixture.instance)));
	assert(!memcmp(&fixture.evidence, &foreign_replacement,
		sizeof(fixture.evidence)));
	assert(!smm_invocation_loader_composition_evidence(
		&fixture.composition, &fixture.evidence));
}

static void test_failures(void)
{
	struct fixture fixture;
	const struct {
		uint32_t point;
		uint32_t action;
	} mutations[] = {
		{ 2, 2 }, { 2, 8 }, { 2, 9 }, { 2, 10 }, { 2, 11 },
		{ 2, 12 }, { 2, 13 }, { 4, 3 }, { 4, 14 }, { 4, 15 },
		{ 4, 16 }, { 4, 17 }, { 4, 18 }, { 4, 19 }, { 4, 20 },
		{ 4, 21 }, { 6, 4 }, { 7, 5 }, { 7, 23 },
		{ 7, 24 }, { 7, 25 }, { 7, 26 }, { 7, 27 }, { 7, 28 },
		{ 7, 29 }, { 7, 30 }, { 4, 31 },
		{ 4, 32 }, { 6, 34 }, { 4, 35 }, { 6, 37 },
	};

	setup(&fixture);
	provider_fail = 1;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert(provider_calls == 1U);
	assert_failed(&fixture);
	assert(unwind_count == 2U && unwind_trace[0] == 102U &&
		unwind_trace[1] == 103U);

	for (size_t index = 0; index < ARRAY_SIZE(mutations); index++) {
		setup(&fixture);
		hook_point = mutations[index].point;
		hook_action = mutations[index].action;
		assert(smm_invocation_loader_compose(&fixture.composition,
			&fixture.topology, &fixture.instance,
			&fixture.evidence) == CB_ERR);
		assert_failed(&fixture);
		assert(unwind_trace[unwind_count - 2U] == 102U);
		assert(unwind_trace[unwind_count - 1U] == 103U);
		for (uint32_t trace = 1; trace < unwind_count; trace++)
			assert(unwind_trace[trace - 1U] < unwind_trace[trace]);
	}
}

static void test_malformed_provider_and_prefixes(void)
{
	struct fixture fixture;
	struct smm_invocation_evidence foreign;

	for (uint32_t malformed = 1; malformed <= 5; malformed++) {
		setup(&fixture);
		provider_malformed = malformed;
		assert(smm_invocation_loader_compose(&fixture.composition,
			&fixture.topology, &fixture.instance,
			&fixture.evidence) == CB_ERR);
		assert_failed(&fixture);
	}

	setup(&fixture);
	fixture.instance.reserved = 1;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert_failed(&fixture);

	setup(&fixture);
	fixture.composition.reserved[1] = 1;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert_failed(&fixture);

	setup(&fixture);
	memset(&fixture.evidence, 0, sizeof(fixture.evidence));
	fixture.evidence.state = SMM_INVOCATION_READY;
	foreign = fixture.evidence;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert(!memcmp(&fixture.evidence, &foreign, sizeof(foreign)));
	assert(!memcmp(&fixture.instance,
		&(const struct smm_invocation_loader_instance) { 0 },
		sizeof(fixture.instance)));
	assert(!memcmp(&fixture.topology,
		&(const struct smm_invocation_topology) { 0 },
		sizeof(fixture.topology)));
}

static void test_invalid_input_is_inert(void)
{
	struct fixture fixture;
	struct fixture before;
	struct smm_invocation_loader_composition *overflow;

	setup(&fixture);
	before = fixture;
	assert(smm_invocation_loader_compose(NULL, &fixture.topology,
		&fixture.instance, &fixture.evidence) == CB_ERR_ARG);
	assert(!memcmp(&fixture, &before, sizeof(fixture)));
	assert(smm_invocation_loader_compose(
		(struct smm_invocation_loader_composition *)
		((uint8_t *)&fixture.composition + 1U), &fixture.topology,
		&fixture.instance, &fixture.evidence) == CB_ERR_ARG);
	assert(!memcmp(&fixture, &before, sizeof(fixture)));
	assert(smm_invocation_loader_compose(&fixture.composition,
		(struct smm_invocation_topology *)&fixture.composition,
		&fixture.instance, &fixture.evidence) == CB_ERR_ARG);
	assert(!memcmp(&fixture, &before, sizeof(fixture)));
	overflow = (struct smm_invocation_loader_composition *)(UINTPTR_MAX -
		sizeof(*overflow) + 2U);
	assert(smm_invocation_loader_compose(overflow, &fixture.topology,
		&fixture.instance, &fixture.evidence) == CB_ERR_ARG);
	assert(!memcmp(&fixture, &before, sizeof(fixture)));
	fixture.composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	before = fixture;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert(!memcmp(&fixture, &before, sizeof(fixture)));
}

struct thread_arg {
	struct fixture *fixture;
	enum cb_err result;
};

static void *compose_thread(void *argument)
{
	struct thread_arg *arg = argument;

	arg->result = smm_invocation_loader_compose(&arg->fixture->composition,
		&arg->fixture->topology, &arg->fixture->instance,
		&arg->fixture->evidence);
	return NULL;
}

static void test_two_composers(void)
{
	struct fixture fixture;
	struct thread_arg owner = { .fixture = &fixture };
	pthread_t thread;

	setup(&fixture);
	hook_point = 1;
	hook_action = 7;
	assert(!pthread_create(&thread, NULL, compose_thread, &owner));
	while (!__atomic_load_n(&hook_entered, __ATOMIC_ACQUIRE))
		__asm__ volatile ("pause");
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	__atomic_store_n(&hook_release, 1U, __ATOMIC_RELEASE);
	assert(!pthread_join(thread, NULL));
	assert(owner.result == CB_SUCCESS);
	assert(provider_calls == 1U);
	assert_ready(&fixture);
}

static void test_observed_ready_is_inert(void)
{
	struct fixture fixture;

	setup(&fixture);
	hook_point = 7;
	hook_action = 6;
	assert(smm_invocation_loader_compose(&fixture.composition,
		&fixture.topology, &fixture.instance, &fixture.evidence) == CB_ERR);
	assert(provider_calls == 1U);
	assert_ready(&fixture);
}

int main(void)
{
	test_success_and_reentry();
	test_forward_order();
	test_accessor_gate();
	test_foreign_evidence_replacement();
	test_failures();
	test_owned_evidence_failures();
	test_evidence_receipt();
	test_malformed_provider_and_prefixes();
	test_invalid_input_is_inert();
	test_two_composers();
	test_observed_ready_is_inert();
	return 0;
}
