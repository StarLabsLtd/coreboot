/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_arm.h>
#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define NONCE ((struct smm_invocation_loader_instance_nonce) { \
	.low = 7U, .high = 0xa5a5a5a5a5a5a5a2ULL })

static struct payload_mm_authvar_presence_transaction_policy captured_policy;
static struct payload_mm_authvar_presence_transaction_binding captured_binding;
static unsigned int provision_calls, prepare_calls, commit_calls, abort_calls;
static unsigned int claim_calls, complete_calls;
static unsigned int dma_calls;
static uint64_t invocation_generation;
static bool prepare_fails, provision_fails;
static struct payload_mm_authvar_presence_arm *canonical_arm;
static const void *denied_base;
static size_t denied_size;
static struct bootmem_reservation_receipt *mutate_receipt;
static struct bootmem_reservation_receipt_authority *mutate_verifier;
static bool mutate_proof_closure, mutate_callback_identity;
static bool mutate_loader_bundle;
static bool mutate_omitted_arm_bytes;
static bool mutate_during_generic_provision;
static bool mutate_initial_arm;
static struct smm_invocation_loader_instance *mutate_initial_instance;
static struct smm_invocation_evidence *mutate_initial_evidence;
static struct smm_invocation_loader_composition alternate_composition;
static struct smm_invocation_loader_instance alternate_instance;
static struct smm_invocation_evidence alternate_evidence;
static unsigned int callback_mutation, complete_mutation;
static unsigned int requested_callback_mutation, requested_proof_mutation;
static unsigned int requested_complete_mutation;
static unsigned int requested_precomplete_corruption;

struct payload_mm_authvar_presence_arm *
platform_payload_mm_authvar_presence_arm(void)
{
	return canonical_arm;
}

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

const struct smm_invocation_evidence *
smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_evidence *evidence)
{
	return composition && evidence &&
		composition->state == SMM_INVOCATION_LOADER_COMPOSITION_READY &&
		composition->owner_attempt == 1U &&
		composition->evidence_identity == (uintptr_t)evidence ? evidence : NULL;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_loader_instance *snapshot)
{
	if (!instance || !snapshot ||
	    instance->state != SMM_INVOCATION_LOADER_INSTANCE_READY)
		return CB_ERR;
	*snapshot = *instance;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_endpoint_validate(
	const struct lb_authvar_presence_endpoint *endpoint)
{
	return endpoint && endpoint->tag == LB_TAG_AUTHVAR_PRESENCE_ENDPOINT &&
		endpoint->size == sizeof(*endpoint) && endpoint->generation &&
		endpoint->transport == LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8 &&
		endpoint->trigger_width == 1U &&
		endpoint->trigger_address == APM_CNT &&
		endpoint->trigger_value == SMM_APMC_AUTHVAR_PRESENCE ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_presence_transaction_provision(
	struct payload_mm_authvar_presence_transaction_slot *slot,
	const struct payload_mm_authvar_presence_transaction_policy *policy,
	const struct payload_mm_authvar_presence_transaction_binding *binding,
	struct bootmem_reservation_receipt_authority *page_verifier,
	struct bootmem_reservation_receipt *page_receipt,
	payload_mm_authvar_protected_storage storage_is_protected,
	void *storage_context)
{
	(void)page_verifier;
	(void)page_receipt;
	provision_calls++;
	if (provision_fails)
		return CB_ERR;
	if (mutate_during_generic_provision)
		mutate_omitted_arm_bytes = true;
	if (!storage_is_protected(storage_context, slot, sizeof(*slot)))
		return CB_ERR;
	captured_policy = *policy;
	captured_binding = *binding;
	bootmem_reservation_receipt_close(page_verifier);
	memset(page_receipt, 0, sizeof(*page_receipt));
	return CB_SUCCESS;
}

void bootmem_reservation_receipt_close(
	struct bootmem_reservation_receipt_authority *authority)
{
	if (authority)
		memset(authority, 0, sizeof(*authority));
}

bool bootmem_reservation_receipt_authority_claim(
	struct bootmem_reservation_receipt_authority *authority,
	struct bootmem_reservation_receipt_authority *snapshot)
{
	uint8_t expected = 3U;

	if (!authority || !snapshot || !__atomic_compare_exchange_n(
		&authority->state, &expected, 4U, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return false;
	*snapshot = *authority;
	snapshot->state = 3U;
	memset(authority, 0, sizeof(*authority));
	authority->state = 4U;
	return snapshot->generation && snapshot->sequence == 1U &&
		snapshot->handle.opaque[0] && snapshot->handle.opaque[1];
}

void payload_mm_authvar_presence_arm_test_before_verifier_claim(
	struct bootmem_reservation_receipt_authority *verifier)
{
	(void)verifier;
}

static bool protected_storage(void *context, const void *object, size_t size)
{
	(void)context;
	if (mutate_initial_arm) {
		canonical_arm->state = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY;
		canonical_arm->reserved = 1U;
		mutate_initial_arm = false;
	}
	if (mutate_initial_instance) {
		mutate_initial_instance->lifecycle++;
		mutate_initial_instance = NULL;
	}
	if (mutate_initial_evidence) {
		mutate_initial_evidence->active_cpus++;
		mutate_initial_evidence = NULL;
	}
	if (mutate_receipt) {
		mutate_receipt->base ^= PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE;
		mutate_receipt = NULL;
	}
	if (mutate_verifier) {
		mutate_verifier->generation++;
		mutate_verifier = NULL;
	}
	if (mutate_callback_identity) {
		canonical_arm->callback_context.identity++;
		mutate_callback_identity = false;
	}
	if (mutate_proof_closure) {
		canonical_arm->protected_storage_context = canonical_arm;
		canonical_arm->sealed_protected_storage_context = canonical_arm;
		mutate_proof_closure = false;
	}
	if (mutate_loader_bundle) {
		canonical_arm->composition = &alternate_composition;
		canonical_arm->sealed_composition = &alternate_composition;
		canonical_arm->instance = &alternate_instance;
		canonical_arm->sealed_instance = &alternate_instance;
		canonical_arm->evidence = &alternate_evidence;
		canonical_arm->sealed_evidence = &alternate_evidence;
		canonical_arm->instance_snapshot = alternate_instance;
		canonical_arm->sealed_instance_snapshot = alternate_instance;
		mutate_loader_bundle = false;
	}
	if (mutate_omitted_arm_bytes) {
		canonical_arm->failure_context[
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX - 1U]++;
		canonical_arm->sealed_failure_context[
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX - 1U]++;
		mutate_omitted_arm_bytes = false;
	}
	if (!object || !size)
		return false;
	if (!denied_base || !denied_size)
		return true;
	return (uintptr_t)object + size <= (uintptr_t)denied_base ||
		(uintptr_t)denied_base + denied_size <= (uintptr_t)object;
}

static enum cb_err original_prepare(void *context,
	const struct payload_mm_authvar_presence_seed *seed, uint64_t generation)
{
	(void)context;
	(void)seed;
	(void)generation;
	prepare_calls++;
	return prepare_fails ? CB_ERR : CB_SUCCESS;
}

static void __noreturn attacker_fail_stop(void *context)
{
	(void)context;
	_exit(42);
}

static enum cb_err original_commit(void *context, uint64_t generation)
{
	(void)context;
	(void)generation;
	commit_calls++;
	switch (callback_mutation) {
	case 1:
		canonical_arm->binding.nonce++;
		break;
	case 2:
		canonical_arm->seed.capability[0]++;
		break;
	case 3:
		canonical_arm->protected_storage_context = canonical_arm;
		break;
	case 4:
		canonical_arm->callback_context.identity++;
		break;
	case 5:
		canonical_arm->failure_callback = NULL;
		break;
	case 6:
		canonical_arm->audit = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_EMPTY;
		break;
	case 7:
		canonical_arm->completed_prepare_generation++;
		break;
	case 8:
		canonical_arm->policy.context_size = UINT32_MAX;
		canonical_arm->sealed_policy.context_size = UINT32_MAX;
		break;
	case 9:
		canonical_arm->failure_context_size = SIZE_MAX;
		canonical_arm->sealed_failure_context_size = SIZE_MAX;
		break;
	case 10:
		canonical_arm->composition = &alternate_composition;
		canonical_arm->instance = &alternate_instance;
		canonical_arm->evidence = &alternate_evidence;
		canonical_arm->instance_snapshot = alternate_instance;
		break;
	case 11:
		canonical_arm->binding.nonce++;
		canonical_arm->sealed_binding.nonce++;
		break;
	case 12:
		canonical_arm->failure_callback = attacker_fail_stop;
		canonical_arm->sealed_failure_callback = attacker_fail_stop;
		canonical_arm->failure_context_size = 1U;
		canonical_arm->sealed_failure_context_size = 1U;
		canonical_arm->failure_context[0] = 0xa5U;
		canonical_arm->sealed_failure_context[0] = 0xa5U;
		break;
	default:
		break;
	}
	return CB_SUCCESS;
}

static enum cb_err original_abort(void *context, uint64_t generation)
{
	(void)context;
	(void)generation;
	abort_calls++;
	return CB_SUCCESS;
}

static bool original_dma(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	dma_calls++;
	return base && size;
}

static enum cb_err original_claim(void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	(void)context;
	claim_calls++;
	if (sentinel != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL)
		return CB_ERR;
	invocation_generation += 2U;
	*invocation =
		(struct payload_mm_authvar_presence_transaction_invocation) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
			.size = sizeof(*invocation), .initiator_cpu = 1U,
			.active_cpus = 4U, .smi_generation = invocation_generation,
			.rendezvous_generation = invocation_generation,
			.rendezvous_proof = { 1U }, .bsp = 1U,
	};
	return CB_SUCCESS;
}

static enum cb_err original_complete(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	(void)context;
	(void)invocation;
	complete_calls++;
	if (complete_mutation == 1U)
		canonical_arm->audit = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_EMPTY;
	else if (complete_mutation == 2U)
		canonical_arm->completed_prepare_generation++;
	return value ? CB_SUCCESS : CB_ERR;
}

static void __noreturn original_fail_stop(void *context)
{
	(void)context;
	__builtin_trap();
}

struct fixture {
	struct smm_invocation_loader_composition composition;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_evidence evidence;
	struct payload_mm_authvar_presence_arm arm;
	struct payload_mm_authvar_presence_transaction_slot slot;
	struct payload_mm_authvar_presence_transaction_policy policy;
	struct payload_mm_authvar_presence_transaction_binding binding;
	struct bootmem_reservation_receipt_authority verifier;
	struct bootmem_reservation_receipt receipt;
};

static void fixture_init(struct fixture *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	canonical_arm = &fixture->arm;
	denied_base = NULL;
	denied_size = 0;
	mutate_receipt = NULL;
	mutate_verifier = NULL;
	mutate_proof_closure = false;
	mutate_callback_identity = false;
	mutate_loader_bundle = false;
	mutate_omitted_arm_bytes = false;
	mutate_during_generic_provision = false;
	mutate_initial_arm = false;
	mutate_initial_instance = NULL;
	mutate_initial_evidence = NULL;
	callback_mutation = 0U;
	complete_mutation = 0U;
	memset(&captured_policy, 0, sizeof(captured_policy));
	memset(&captured_binding, 0, sizeof(captured_binding));
	fixture->composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	fixture->composition.owner_attempt = 1U;
	fixture->composition.evidence_identity = (uintptr_t)&fixture->evidence;
	fixture->instance = (struct smm_invocation_loader_instance) {
		.state = SMM_INVOCATION_LOADER_INSTANCE_READY,
		.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION,
		.size = sizeof(fixture->instance), .lifecycle = 3U,
		.loader_instance_nonce = NONCE,
	};
	fixture->evidence.state = SMM_INVOCATION_READY;
	fixture->evidence.active_cpus = 4U;
	fixture->evidence.bsp_cpu = 1U;
	fixture->evidence.loader_instance_nonce = NONCE;
	fixture->evidence.loader_lifecycle = 3U;
	fixture->policy =
		(struct payload_mm_authvar_presence_transaction_policy) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION,
			.size = sizeof(fixture->policy), .prepare = original_prepare,
			.commit = original_commit, .abort = original_abort,
			.dma_protected = original_dma,
			.claim_invocation = original_claim,
			.complete_invocation = original_complete,
			.fail_stop = original_fail_stop,
	};
	fixture->binding =
		(struct payload_mm_authvar_presence_transaction_binding) {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
			.size = sizeof(fixture->binding), .generation = 11U,
			.transaction_id = 12U, .nonce = 13U, .initiator_cpu = 1U,
			.maximum_cpus = 4U, .capability = { 1U },
	};
	fixture->receipt.base = 0x200000U;
	fixture->receipt.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE;
	fixture->verifier.secret[0] = 1U;
	fixture->verifier.generation = fixture->binding.generation;
	fixture->verifier.handle.opaque[0] = 1U;
	fixture->verifier.handle.opaque[1] = 2U;
	fixture->verifier.sequence = 1U;
	fixture->verifier.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT;
	fixture->verifier.state = 3U;
}

static struct payload_mm_authvar_presence_seed seed(
	const struct fixture *fixture)
{
	return (struct payload_mm_authvar_presence_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION,
		.size = sizeof(struct payload_mm_authvar_presence_seed),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
			.size = sizeof(struct lb_authvar_presence_endpoint),
			.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_authvar_presence_endpoint),
			.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
			.generation = fixture->binding.generation,
			.communication_base = 0x100000U,
			.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
			.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
			.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
			.trigger_width = 1U,
			.trigger_address = APM_CNT,
			.trigger_value = SMM_APMC_AUTHVAR_PRESENCE,
			.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
			.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
		},
		.backing = {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
			.size = sizeof(struct payload_mm_authvar_presence_backing),
			.base = 0x100000U,
			.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
			.generation = fixture->binding.generation,
			.tag = BM_MEM_RESERVED,
		},
		.capability = { 1U },
	};
}

static void provision(struct fixture *fixture)
{
	assert(payload_mm_authvar_presence_arm_provision(&fixture->arm,
		&fixture->composition, &fixture->instance, &fixture->evidence,
		protected_storage, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&fixture->arm,
		&fixture->slot, &fixture->policy, &fixture->binding,
		&fixture->verifier, &fixture->receipt, protected_storage, NULL) ==
		CB_SUCCESS);
	assert(!memcmp(&captured_binding, &fixture->binding,
		sizeof(captured_binding)));
}

static void claim(struct payload_mm_authvar_presence_transaction_invocation *inv)
{
	assert(captured_policy.claim_invocation(captured_policy.context,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_RAX_SENTINEL, inv) ==
		CB_SUCCESS);
}

static void complete(
	const struct payload_mm_authvar_presence_transaction_invocation *inv,
	uint32_t decision)
{
	assert(captured_policy.complete_invocation(captured_policy.context, inv,
		payload_mm_authvar_presence_transaction_rax(&captured_binding,
			decision)) == CB_SUCCESS);
}

static void happy_commit(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	claim(&invocation);
	assert(captured_policy.commit(captured_policy.context,
		f.binding.generation) == CB_SUCCESS);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY);
}

static void abort_case(bool reject, bool call_abort)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;
	unsigned int before = abort_calls;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	if (reject) {
		request.endpoint.trigger_value = 0xfeU;
		assert(captured_policy.prepare(captured_policy.context, &request,
			f.binding.generation) == CB_ERR);
	} else if (call_abort) {
		prepare_fails = true;
		assert(captured_policy.prepare(captured_policy.context, &request,
			f.binding.generation) == CB_ERR);
		prepare_fails = false;
	}
	if (call_abort)
		assert(captured_policy.abort(captured_policy.context,
			f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	assert(abort_calls == before + (!reject && call_abort));
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
	assert(!f.arm.binding.generation && !f.arm.seed.revision &&
		!f.arm.policy.prepare && !f.arm.composition &&
		f.arm.failure_callback == original_fail_stop);
}

static void provisioning_failure_consumes_arm(void)
{
	struct fixture f;

	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_SUCCESS);
	provision_fails = true;
	assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
		&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
		protected_storage, NULL) == CB_ERR);
	provision_fails = false;
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void mismatched_cpu_binding_is_terminal(void)
{
	struct fixture f;

	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_SUCCESS);
	f.binding.maximum_cpus++;
	assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
		&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
		protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void hostile_provisioning_inputs_are_terminal(void)
{
	struct fixture f;

	fixture_init(&f);
	f.arm.owner_attempt = UINT32_MAX;
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_SUCCESS);
	denied_base = &f.slot;
	denied_size = sizeof(f.slot);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
		&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
		protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
	fixture_init(&f);
	f.evidence.loader_instance_nonce.high++;
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_ERR);
}

static void initial_proof_arm_mutation_is_terminal(void)
{
	struct fixture f;

	fixture_init(&f);
	mutate_initial_arm = true;
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void initial_proof_loader_input_mutations_are_terminal(void)
{
	struct fixture f;

	fixture_init(&f);
	mutate_initial_instance = &f.instance;
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
	fixture_init(&f);
	mutate_initial_evidence = &f.evidence;
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void transaction_bootstrap_mutations_are_terminal(void)
{
	struct fixture f;

	for (unsigned int mutation = 0; mutation < 4U; mutation++) {
		fixture_init(&f);
		assert(payload_mm_authvar_presence_arm_provision(&f.arm,
			&f.composition, &f.instance, &f.evidence,
			protected_storage, NULL) == CB_SUCCESS);
		if (mutation == 0U)
			mutate_proof_closure = true;
		else if (mutation == 1U)
			mutate_callback_identity = true;
		else if (mutation == 2U)
			mutate_loader_bundle = true;
		else
			mutate_omitted_arm_bytes = true;
		assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
			&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
			protected_storage, NULL) == CB_ERR);
		assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
		assert(!f.arm.binding.capability[0] && !f.arm.policy.prepare &&
			!f.arm.policy_context[0]);
	}
}

typedef void (*fail_stop_case)(void);

static void expect_fail_stop(fail_stop_case test)
{
	int status;
	pid_t child = fork();

	assert(child >= 0);
	if (!child) {
		test();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFSIGNALED(status));
	assert(WTERMSIG(status) == SIGILL || WTERMSIG(status) == SIGABRT);
}

static void wrong_rax_after_claim(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	assert(captured_policy.complete_invocation(captured_policy.context,
		&invocation, payload_mm_authvar_presence_transaction_rax(
			&f.binding, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT)) ==
		CB_ERR);
}

static void changed_invocation_after_claim(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	claim(&invocation);
	invocation.rendezvous_proof[0] ^= 1U;
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
}

static void replay_prepare_completion(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
}

static void replay_generation_for_decision(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	invocation_generation -= 2U;
	claim(&invocation);
}

static void retained_fail_stop_after_abort(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	request.endpoint.trigger_value = 0xfeU;
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_ERR);
	assert(captured_policy.abort(captured_policy.context,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
	captured_policy.fail_stop(captured_policy.context);
}

static void page_overlap_is_terminal(void)
{
	struct fixture f;

	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_SUCCESS);
	f.receipt.base = (uintptr_t)&f.arm &
		~((uintptr_t)PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE - 1U);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
		&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
		protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void fresh_resume_nonce_is_accepted(void)
{
	struct fixture f;

	fixture_init(&f);
	f.instance.loader_instance_nonce.low++;
	f.evidence.loader_instance_nonce = f.instance.loader_instance_nonce;
	provision(&f);
}

static void copied_arm_is_rejected(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_arm copy;

	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_SUCCESS);
	copy = f.arm;
	assert(payload_mm_authvar_presence_arm_transaction_provision(&copy,
		&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
		protected_storage, NULL) == CB_ERR);
	assert(copy.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_LOADER_READY);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_LOADER_READY);
}

static void denied_platform_provider_is_rejected(void)
{
	struct fixture f;

	fixture_init(&f);
	denied_base =
		(const void *)(uintptr_t)platform_payload_mm_authvar_presence_arm;
	denied_size = 1U;
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void denied_storage_callback_is_rejected(void)
{
	struct fixture f;

	fixture_init(&f);
	denied_base = (const void *)(uintptr_t)protected_storage;
	denied_size = 1U;
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void original_context_overlap_is_terminal(void)
{
	struct fixture f;

	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_SUCCESS);
	f.policy.context = &f.slot;
	f.policy.context_size = 1U;
	assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
		&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
		protected_storage, NULL) == CB_ERR);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
}

static void protected_transaction_inputs_are_enforced(void)
{
	struct fixture f;
	const void *callbacks[] = {
		(const void *)(uintptr_t)original_prepare,
		(const void *)(uintptr_t)original_commit,
		(const void *)(uintptr_t)original_abort,
		(const void *)(uintptr_t)original_dma,
		(const void *)(uintptr_t)original_claim,
		(const void *)(uintptr_t)original_complete,
		(const void *)(uintptr_t)original_fail_stop,
	};

	for (unsigned int target = 0; target < 6U + ARRAY_SIZE(callbacks);
	     target++) {
		fixture_init(&f);
		assert(payload_mm_authvar_presence_arm_provision(&f.arm,
			&f.composition, &f.instance, &f.evidence,
			protected_storage, NULL) == CB_SUCCESS);
		switch (target) {
		case 0:
			denied_base = &f.arm;
			denied_size = sizeof(f.arm);
			break;
		case 1:
			denied_base = &f.slot;
			denied_size = sizeof(f.slot);
			break;
		case 2:
			denied_base = &f.policy;
			denied_size = sizeof(f.policy);
			break;
		case 3:
			denied_base = &f.binding;
			denied_size = sizeof(f.binding);
			break;
		case 4:
			denied_base = &f.verifier;
			denied_size = sizeof(f.verifier);
			break;
		case 5:
			denied_base = &f.receipt;
			denied_size = sizeof(f.receipt);
			break;
		default:
			denied_base = callbacks[target - 6U];
			denied_size = 1U;
			break;
		}
		assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
			&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
			protected_storage, NULL) == CB_ERR);
		assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
	}
}

static void transaction_object_overlaps_are_rejected(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_transaction_slot *slot;
	const struct payload_mm_authvar_presence_transaction_policy *policy;
	const struct payload_mm_authvar_presence_transaction_binding *binding;
	struct bootmem_reservation_receipt_authority *verifier;
	struct bootmem_reservation_receipt *receipt;

	for (unsigned int target = 0; target < 4U; target++) {
		fixture_init(&f);
		assert(payload_mm_authvar_presence_arm_provision(&f.arm,
			&f.composition, &f.instance, &f.evidence,
			protected_storage, NULL) == CB_SUCCESS);
		slot = &f.slot;
		policy = &f.policy;
		binding = &f.binding;
		verifier = &f.verifier;
		receipt = &f.receipt;
		switch (target) {
		case 0:
			policy = (const void *)slot;
			break;
		case 1:
			binding = (const void *)policy;
			break;
		case 2:
			receipt = (void *)verifier;
			break;
		default:
			binding = (const void *)slot;
			break;
		}
		assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
			slot, policy, binding, verifier, receipt,
			protected_storage, NULL) == CB_ERR);
		assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
	}
}

static void loader_overlaps_are_rejected(void)
{
	struct fixture f;

	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm,
		(const void *)&f.arm, &f.instance, &f.evidence,
		protected_storage, NULL) == CB_ERR);
	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		(const void *)&f.composition, &f.evidence,
		protected_storage, NULL) == CB_ERR);
	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, (const void *)&f.instance,
		protected_storage, NULL) == CB_ERR);
}

static void dma_after_ready(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	claim(&invocation);
	assert(captured_policy.commit(captured_policy.context,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
	assert(f.arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY);
	(void)captured_policy.dma_protected(captured_policy.context, 1U, 1U);
}

static void mutation_during_commit(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	claim(&invocation);
	callback_mutation = requested_callback_mutation;
	(void)captured_policy.commit(captured_policy.context, f.binding.generation);
}

static void proof_mutation_case(void)
{
	struct fixture f;

	fixture_init(&f);
	assert(payload_mm_authvar_presence_arm_provision(&f.arm, &f.composition,
		&f.instance, &f.evidence, protected_storage, NULL) == CB_SUCCESS);
	if (requested_proof_mutation == 1U)
		mutate_receipt = &f.receipt;
	else if (requested_proof_mutation == 2U)
		mutate_verifier = &f.verifier;
	else if (requested_proof_mutation == 3U)
		mutate_proof_closure = true;
	else if (requested_proof_mutation == 6U)
		mutate_callback_identity = true;
	else if (requested_proof_mutation == 7U)
		mutate_loader_bundle = true;
	else if (requested_proof_mutation == 8U)
		mutate_omitted_arm_bytes = true;
	else if (requested_proof_mutation == 9U)
		mutate_during_generic_provision = true;
	assert(payload_mm_authvar_presence_arm_transaction_provision(&f.arm,
		&f.slot, &f.policy, &f.binding, &f.verifier, &f.receipt,
		protected_storage, NULL) == CB_SUCCESS);
	if (requested_proof_mutation == 4U) {
		mutate_proof_closure = true;
		(void)captured_policy.dma_protected(captured_policy.context, 1U, 1U);
	} else if (requested_proof_mutation == 5U) {
		mutate_callback_identity = true;
		(void)captured_policy.dma_protected(captured_policy.context, 1U, 1U);
	}
}

static void mutation_during_commit_completion(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	claim(&invocation);
	assert(captured_policy.commit(captured_policy.context,
		f.binding.generation) == CB_SUCCESS);
	complete_mutation = requested_complete_mutation;
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
}

static void corruption_before_commit_completion(void)
{
	struct fixture f;
	struct payload_mm_authvar_presence_seed request;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture_init(&f);
	provision(&f);
	request = seed(&f);
	claim(&invocation);
	assert(captured_policy.prepare(captured_policy.context, &request,
		f.binding.generation) == CB_SUCCESS);
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE);
	claim(&invocation);
	assert(captured_policy.commit(captured_policy.context,
		f.binding.generation) == CB_SUCCESS);
	if (requested_precomplete_corruption == 1U)
		f.arm.audit = PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_AUDIT_EMPTY;
	else
		f.arm.completed_prepare_generation = 0U;
	complete(&invocation, PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT);
}

int main(void)
{
	happy_commit();
	abort_case(true, true);
	abort_case(false, true);
	abort_case(false, false);
	provisioning_failure_consumes_arm();
	mismatched_cpu_binding_is_terminal();
	hostile_provisioning_inputs_are_terminal();
	initial_proof_arm_mutation_is_terminal();
	initial_proof_loader_input_mutations_are_terminal();
	transaction_bootstrap_mutations_are_terminal();
	page_overlap_is_terminal();
	fresh_resume_nonce_is_accepted();
	copied_arm_is_rejected();
	denied_platform_provider_is_rejected();
	denied_storage_callback_is_rejected();
	original_context_overlap_is_terminal();
	protected_transaction_inputs_are_enforced();
	loader_overlaps_are_rejected();
	transaction_object_overlaps_are_rejected();
	expect_fail_stop(wrong_rax_after_claim);
	expect_fail_stop(changed_invocation_after_claim);
	expect_fail_stop(replay_prepare_completion);
	expect_fail_stop(replay_generation_for_decision);
	expect_fail_stop(retained_fail_stop_after_abort);
	expect_fail_stop(dma_after_ready);
	for (requested_callback_mutation = 1U;
	     requested_callback_mutation <= 12U; requested_callback_mutation++)
		expect_fail_stop(mutation_during_commit);
	for (requested_proof_mutation = 1U; requested_proof_mutation <= 2U;
	     requested_proof_mutation++)
		expect_fail_stop(proof_mutation_case);
	for (requested_proof_mutation = 4U; requested_proof_mutation <= 5U;
	     requested_proof_mutation++)
		expect_fail_stop(proof_mutation_case);
	requested_proof_mutation = 9U;
	expect_fail_stop(proof_mutation_case);
	for (requested_complete_mutation = 1U;
	     requested_complete_mutation <= 2U; requested_complete_mutation++)
		expect_fail_stop(mutation_during_commit_completion);
	for (requested_precomplete_corruption = 1U;
	     requested_precomplete_corruption <= 2U;
	     requested_precomplete_corruption++)
		expect_fail_stop(corruption_before_commit_completion);
	assert(provision_calls >= 6U && prepare_calls >= 2U && commit_calls >= 1U);
	assert(claim_calls >= 5U && complete_calls >= 5U);
	assert(dma_calls == 0U);
	return 0;
}
