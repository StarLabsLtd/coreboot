/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_arm.h>
#include <bootmem_reservation_receipt_internal.h>
#include <cpu/x86/apm.h>
#include <cpu/x86/smm_command.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static struct payload_mm_authvar_presence_arm arm;
static struct payload_mm_authvar_presence_transaction_slot slot;
static struct payload_mm_authvar_presence_transaction_page page
	__aligned(PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PAGE_SIZE);
static uint64_t smi_generation;
static unsigned int abort_calls;
static bool prepare_fails;
static bool mutate_during_generic_proof, compete_for_verifier;
static bool competing_claim_won;
static struct bootmem_reservation_receipt_authority competing_authority;
static pthread_mutex_t complete_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t complete_condition = PTHREAD_COND_INITIALIZER;
static bool block_complete, complete_entered, release_complete;

void payload_mm_authvar_presence_transaction_test_after_provision_claim(
	const struct payload_mm_authvar_presence_transaction_policy *policy)
{
	(void)policy;
}

void payload_mm_authvar_presence_transaction_test_after_dispatch_owner(
	uint32_t state)
{
	(void)state;
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

struct payload_mm_authvar_presence_arm *
platform_payload_mm_authvar_presence_arm(void)
{
	return &arm;
}

struct payload_mm_authvar_presence_transaction_slot *
smm_get_payload_mm_authvar_presence_transaction_slot(void)
{
	return &slot;
}

const struct smm_invocation_evidence *
smm_invocation_loader_composition_evidence(
	const struct smm_invocation_loader_composition *composition,
	const struct smm_invocation_evidence *evidence)
{
	return composition && composition->state ==
		SMM_INVOCATION_LOADER_COMPOSITION_READY &&
		composition->evidence_identity == (uintptr_t)evidence ? evidence : NULL;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *instance,
	struct smm_invocation_loader_instance *snapshot)
{
	if (!instance || instance->state != SMM_INVOCATION_LOADER_INSTANCE_READY)
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

static bool protected_storage(void *context, const void *object, size_t size)
{
	(void)context;
	if (mutate_during_generic_proof &&
	    arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND) {
		arm.callback_context.identity++;
		mutate_during_generic_proof = false;
	}
	return object && size;
}

void payload_mm_authvar_presence_arm_test_before_verifier_claim(
	struct bootmem_reservation_receipt_authority *verifier)
{
	if (compete_for_verifier) {
		competing_claim_won =
			bootmem_reservation_receipt_authority_claim(verifier,
				&competing_authority);
		compete_for_verifier = false;
	}
}

static enum cb_err prepare(void *context,
	const struct payload_mm_authvar_presence_seed *seed, uint64_t generation)
{
	(void)context;
	return !prepare_fails && seed && seed->endpoint.generation == generation ?
		CB_SUCCESS : CB_ERR;
}

static enum cb_err decide(void *context, uint64_t generation)
{
	(void)context;
	return generation ? CB_SUCCESS : CB_ERR;
}

static enum cb_err abort_decision(void *context, uint64_t generation)
{
	abort_calls++;
	return decide(context, generation);
}

static bool dma_protected(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	return base == (uintptr_t)&page && size == sizeof(page);
}

static enum cb_err claim(void *context, uint64_t sentinel,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	(void)context;
	if (sentinel != PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_SENTINEL)
		return CB_ERR;
	smi_generation += 2U;
	*invocation =
		(struct payload_mm_authvar_presence_transaction_invocation) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
			.size = sizeof(*invocation), .initiator_cpu = 0U,
			.active_cpus = 1U, .smi_generation = smi_generation,
			.rendezvous_generation = smi_generation,
			.rendezvous_proof = { 1U }, .bsp = 1U,
	};
	return CB_SUCCESS;
}

static enum cb_err complete(void *context,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	(void)context;
	if (block_complete) {
		assert(!pthread_mutex_lock(&complete_lock));
		complete_entered = true;
		assert(!pthread_cond_broadcast(&complete_condition));
		while (!release_complete)
			assert(!pthread_cond_wait(&complete_condition, &complete_lock));
		assert(!pthread_mutex_unlock(&complete_lock));
	}
	return invocation && value ? CB_SUCCESS : CB_ERR;
}

static void __noreturn fail_stop(void *context)
{
	(void)context;
	__builtin_trap();
}

static void *dispatch_thread(void *unused)
{
	(void)unused;
	return (void *)(uintptr_t)payload_mm_authvar_presence_transaction_dispatch(
		&slot);
}

int main(void)
{
	int child_status;
	pid_t child;
	pthread_t thread;
	void *thread_status;
	uint64_t prepare_generation;
	struct smm_invocation_loader_composition composition = { 0 };
	struct smm_invocation_loader_instance instance = { 0 };
	struct smm_invocation_evidence evidence = { 0 };
	struct payload_mm_authvar_presence_transaction_policy policy = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_POLICY_REVISION,
		.size = sizeof(policy), .prepare = prepare, .commit = decide,
		.abort = abort_decision, .dma_protected = dma_protected,
		.claim_invocation = claim, .complete_invocation = complete,
		.fail_stop = fail_stop,
	};
	struct payload_mm_authvar_presence_transaction_binding binding = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
		.size = sizeof(binding), .generation = 9U, .transaction_id = 10U,
		.nonce = 11U, .initiator_cpu = 0U, .maximum_cpus = 1U,
		.capability = { 1U },
	};
	struct bootmem_reservation_receipt_authority verifier = {
		0 };
	struct bootmem_reservation_receipt_authority losing_authority = { 0 };
	struct bootmem_reservation_receipt_authority signer = { 0 };
	struct bootmem_reservation_receipt receipt = { 0 };
	struct bootmem_aligned_reservation_handle handle = {
		.opaque = { 1U, 2U },
	};
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE] = { 1U };
	struct payload_mm_authvar_presence_seed seed = {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION,
		.size = sizeof(seed),
		.endpoint = {
			.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
			.size = sizeof(struct lb_authvar_presence_endpoint),
			.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
			.header_size = sizeof(struct lb_authvar_presence_endpoint),
			.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS, .generation = 9U,
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
			.generation = 9U, .tag = BM_MEM_RESERVED,
		},
		.capability = { 1U },
	};

	instance.state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance.revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance.size = sizeof(instance);
	instance.lifecycle = 1U;
	instance.loader_instance_nonce.low = 1U;
	composition.state = SMM_INVOCATION_LOADER_COMPOSITION_READY;
	composition.owner_attempt = 1U;
	composition.evidence_identity = (uintptr_t)&evidence;
	evidence.state = SMM_INVOCATION_READY;
	evidence.active_cpus = 1U;
	evidence.loader_lifecycle = 1U;
	evidence.loader_instance_nonce = instance.loader_instance_nonce;
	assert(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, binding.generation,
		&handle) == CB_SUCCESS);
	receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = binding.generation,
		.sequence = 1U,
		.handle = handle,
		.base = (uintptr_t)&page,
		.bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(signer.secret, &receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt.mac) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_provision(&arm, &composition,
		&instance, &evidence, protected_storage, NULL) == CB_SUCCESS);
	child = fork();
	assert(child >= 0);
	if (!child) {
		mutate_during_generic_proof = true;
		(void)payload_mm_authvar_presence_arm_transaction_provision(&arm,
			&slot, &policy, &binding, &verifier, &receipt,
			protected_storage, NULL);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFSIGNALED(child_status));
	assert(WTERMSIG(child_status) == SIGILL ||
		WTERMSIG(child_status) == SIGABRT);
	child = fork();
	assert(child >= 0);
	if (!child) {
		struct bootmem_reservation_receipt_authority loser = { 0 };
		struct payload_mm_authvar_presence_transaction_binding zero_binding = { 0 };
		struct payload_mm_authvar_presence_transaction_policy zero_policy = { 0 };
		uint8_t zero_context[PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_CONTEXT_MAX]
			= { 0 };

		compete_for_verifier = true;
		assert(payload_mm_authvar_presence_arm_transaction_provision(&arm,
			&slot, &policy, &binding, &verifier, &receipt,
			protected_storage, NULL) == CB_ERR);
		assert(competing_claim_won);
		assert(!bootmem_reservation_receipt_authority_claim(&verifier,
			&loser));
		assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_POISONED);
		assert(!memcmp(&arm.binding, &zero_binding, sizeof(zero_binding)));
		assert(!memcmp(&arm.sealed_binding, &zero_binding,
			sizeof(zero_binding)));
		assert(!memcmp(&arm.policy, &zero_policy, sizeof(zero_policy)));
		assert(!memcmp(&arm.sealed_policy, &zero_policy,
			sizeof(zero_policy)));
		assert(!memcmp(arm.policy_context, zero_context,
			sizeof(zero_context)));
		assert(!memcmp(arm.sealed_policy_context, zero_context,
			sizeof(zero_context)));
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&arm, &slot,
		&policy, &binding, &verifier, &receipt, protected_storage, NULL) ==
		CB_SUCCESS);
	assert(!bootmem_reservation_receipt_authority_claim(&verifier,
		&losing_authority));
	page.request.binding = binding;
	page.request.decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	page.request.seed = seed;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	prepare_generation = smi_generation;
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE, &page.ack,
		payload_mm_authvar_presence_transaction_result(&binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE)));
	memset(&page, 0, sizeof(page));
	page.request.binding = binding;
	page.request.decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT;
	block_complete = true;
	assert(!pthread_create(&thread, NULL, dispatch_thread, NULL));
	assert(!pthread_mutex_lock(&complete_lock));
	while (!complete_entered)
		assert(!pthread_cond_wait(&complete_condition, &complete_lock));
	assert(smi_generation > prepare_generation);
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND);
	release_complete = true;
	assert(!pthread_cond_broadcast(&complete_condition));
	assert(!pthread_mutex_unlock(&complete_lock));
	assert(!pthread_join(thread, &thread_status));
	assert((enum cb_err)(uintptr_t)thread_status == CB_SUCCESS);
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_READY);
	assert(payload_mm_authvar_presence_transaction_dispatch_enabled(&slot,
		binding.generation));

	/* A malformed first request owns an invocation but not the authority. */
	memset(&arm, 0, sizeof(arm));
	memset(&slot, 0, sizeof(slot));
	memset(&page, 0, sizeof(page));
	memset(&signer, 0, sizeof(signer));
	memset(&verifier, 0, sizeof(verifier));
	memset(&receipt, 0, sizeof(receipt));
	memset(secret, 0, sizeof(secret));
	secret[0] = 1U;
	assert(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, binding.generation,
		&handle) == CB_SUCCESS);
	receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = binding.generation, .sequence = 1U, .handle = handle,
		.base = (uintptr_t)&page, .bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(signer.secret, &receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt.mac) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_provision(&arm, &composition,
		&instance, &evidence, protected_storage, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&arm, &slot,
		&policy, &binding, &verifier, &receipt, protected_storage, NULL) ==
		CB_SUCCESS);
	page.request.binding = binding;
	page.request.decision = UINT32_MAX;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &page.ack,
		payload_mm_authvar_presence_transaction_result(&binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT)));
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
	assert(abort_calls == 0U);

	/* A wrapper-rejected seed aborts without entering the original abort. */
	memset(&arm, 0, sizeof(arm));
	memset(&slot, 0, sizeof(slot));
	memset(&page, 0, sizeof(page));
	memset(&signer, 0, sizeof(signer));
	memset(&verifier, 0, sizeof(verifier));
	memset(&receipt, 0, sizeof(receipt));
	memset(secret, 0, sizeof(secret));
	secret[0] = 1U;
	assert(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, binding.generation,
		&handle) == CB_SUCCESS);
	receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = binding.generation, .sequence = 1U, .handle = handle,
		.base = (uintptr_t)&page, .bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(signer.secret, &receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt.mac) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_provision(&arm, &composition,
		&instance, &evidence, protected_storage, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&arm, &slot,
		&policy, &binding, &verifier, &receipt, protected_storage, NULL) ==
		CB_SUCCESS);
	page.request.binding = binding;
	page.request.decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	page.request.seed = seed;
	page.request.seed.endpoint.trigger_value = 0xfeU;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &page.ack,
		payload_mm_authvar_presence_transaction_result(&binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT)));
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
	assert(abort_calls == 0U);

	/* Original PREPARE rejection transfers abort authority exactly once. */
	memset(&arm, 0, sizeof(arm));
	memset(&slot, 0, sizeof(slot));
	memset(&page, 0, sizeof(page));
	memset(&signer, 0, sizeof(signer));
	memset(&verifier, 0, sizeof(verifier));
	memset(&receipt, 0, sizeof(receipt));
	memset(secret, 0, sizeof(secret));
	secret[0] = 1U;
	assert(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, binding.generation,
		&handle) == CB_SUCCESS);
	receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = binding.generation, .sequence = 1U, .handle = handle,
		.base = (uintptr_t)&page, .bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(signer.secret, &receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt.mac) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_provision(&arm, &composition,
		&instance, &evidence, protected_storage, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&arm, &slot,
		&policy, &binding, &verifier, &receipt, protected_storage, NULL) ==
		CB_SUCCESS);
	page.request.binding = binding;
	page.request.decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	page.request.seed = seed;
	prepare_fails = true;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	prepare_fails = false;
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &page.ack,
		payload_mm_authvar_presence_transaction_result(&binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT)));
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
	assert(abort_calls == 1U);

	/* Explicit ABORT uses a newer claim and stays BOUND through completion. */
	memset(&arm, 0, sizeof(arm));
	memset(&slot, 0, sizeof(slot));
	memset(&page, 0, sizeof(page));
	memset(&signer, 0, sizeof(signer));
	memset(&verifier, 0, sizeof(verifier));
	memset(&receipt, 0, sizeof(receipt));
	memset(secret, 0, sizeof(secret));
	secret[0] = 1U;
	assert(bootmem_reservation_receipt_provision(&signer, &verifier, secret,
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, binding.generation,
		&handle) == CB_SUCCESS);
	receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = binding.generation, .sequence = 1U, .handle = handle,
		.base = (uintptr_t)&page, .bytes = sizeof(page),
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(signer.secret, &receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt.mac) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_provision(&arm, &composition,
		&instance, &evidence, protected_storage, NULL) == CB_SUCCESS);
	assert(payload_mm_authvar_presence_arm_transaction_provision(&arm, &slot,
		&policy, &binding, &verifier, &receipt, protected_storage, NULL) ==
		CB_SUCCESS);
	page.request.binding = binding;
	page.request.decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE;
	page.request.seed = seed;
	assert(payload_mm_authvar_presence_transaction_dispatch(&slot) == CB_SUCCESS);
	prepare_generation = smi_generation;
	memset(&page, 0, sizeof(page));
	page.request.binding = binding;
	page.request.decision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT;
	block_complete = true;
	complete_entered = false;
	release_complete = false;
	assert(!pthread_create(&thread, NULL, dispatch_thread, NULL));
	assert(!pthread_mutex_lock(&complete_lock));
	while (!complete_entered)
		assert(!pthread_cond_wait(&complete_condition, &complete_lock));
	assert(smi_generation > prepare_generation);
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_BOUND);
	release_complete = true;
	assert(!pthread_cond_broadcast(&complete_condition));
	assert(!pthread_mutex_unlock(&complete_lock));
	assert(!pthread_join(thread, &thread_status));
	assert((enum cb_err)(uintptr_t)thread_status == CB_SUCCESS);
	assert(payload_mm_authvar_presence_transaction_ack_valid(&binding,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &page.ack,
		payload_mm_authvar_presence_transaction_result(&binding,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT)));
	assert(arm.state == PAYLOAD_MM_AUTHVAR_PRESENCE_ARM_ABORTED);
	assert(abort_calls == 2U);
	return 0;
}
