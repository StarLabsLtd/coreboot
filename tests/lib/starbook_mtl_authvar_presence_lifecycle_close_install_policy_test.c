/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_backing.h>
#include <boot/payload_mm_authvar_presence_lifecycle_close_transport.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <pthread.h>
#include <setjmp.h>
#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

void mock_assert(const int result, const char *const expression,
	const char *const file, const int line)
{
	(void)expression;
	(void)file;
	(void)line;
	if (!result)
		__builtin_trap();
}

static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance test_instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology test_topology;
static struct smm_invocation_save_state_ops test_active_ops;
static struct smm_invocation_runtime_binding test_runtime;
static struct starbook_mtl_dma_smm_receipt dma_receipt;
static struct smm_dma_receipt_memory dma_memory;
static struct bootmem_reservation_receipt_authority test_verifier;
static struct payload_mm_authvar_presence_lifecycle_close_install_frame test_frame;
static unsigned int verifier_calls;
static unsigned int dma_binding_calls;
static bool verify_ok;
static bool protected_ok;
static bool mutate_during_verify;
static jmp_buf fail_stop_jump;
static bool fail_stop_expected;

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	*view = (const void *)(uintptr_t)0x1234;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base, size_t size)
{
	return view && base && size && protected_ok ? CB_SUCCESS : CB_ERR;
}

enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding)
{
	*binding = test_runtime;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *source,
	struct smm_invocation_loader_instance *snapshot)
{
	if (source != &test_instance)
		return CB_ERR;
	*snapshot = test_instance;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_read(
	const struct smm_invocation_topology *source,
	struct smm_invocation_topology *snapshot)
{
	if (source != &test_topology)
		return CB_ERR;
	*snapshot = test_topology;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding)
{
	dma_binding_calls++;
	binding->receipt = &dma_receipt;
	return CB_SUCCESS;
}

bool smm_get_dma_receipt_memory(const struct smm_dma_receipt_memory **memory)
{
	*memory = &dma_memory;
	return true;
}

struct bootmem_reservation_receipt_authority *
smm_get_payload_mm_authvar_presence_lifecycle_close_backing_verifier(void)
{
	return &test_verifier;
}

enum cb_err bootmem_reservation_receipt_verify_consume_exact_tag(
	struct bootmem_reservation_receipt_authority *authority,
	struct bootmem_reservation_receipt *receipt, enum bootmem_type expected_tag)
{
	verifier_calls++;
	if (mutate_during_verify)
		test_frame.request.backing_base++;
	memset(authority, 0, sizeof(*authority));
	memset(receipt, 0, sizeof(*receipt));
	return verify_ok && expected_tag == BM_MEM_RESERVED ? CB_SUCCESS : CB_ERR;
}

void __noreturn
platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop(void)
{
	assert(fail_stop_expected);
	longjmp(fail_stop_jump, 1);
}

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_install_policy.c"

static void fixture(void)
{
	memset(&owner, 0, sizeof(owner));
	memset(&composition, 0, sizeof(composition));
	memset(&test_instance, 0, sizeof(test_instance));
	memset(&evidence, 0, sizeof(evidence));
	memset(&test_topology, 0, sizeof(test_topology));
	memset(&test_active_ops, 0, sizeof(test_active_ops));
	memset(&dma_receipt, 0, sizeof(dma_receipt));
	memset(&dma_memory, 0, sizeof(dma_memory));
	memset(&test_verifier, 0xa5, sizeof(test_verifier));
	memset(&test_frame, 0, sizeof(test_frame));
	verifier_calls = 0;
	dma_binding_calls = 0;
	verify_ok = true;
	protected_ok = true;
	mutate_during_verify = false;
	fail_stop_expected = false;
	test_instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	test_instance.loader_instance_nonce.low = 0x123456789abcdef0ULL;
	test_instance.loader_instance_nonce.high = 0xfedcba9876543210ULL;
	evidence.closed_generation = 41;
	test_topology.active_cpus = 4;
	test_topology.bsp_cpu = 1;
	test_runtime = (struct smm_invocation_runtime_binding) {
		.composition = &composition,
		.instance = &test_instance,
		.evidence = &evidence,
		.topology = &test_topology,
	};
	dma_receipt.loader_instance_nonce = test_instance.loader_instance_nonce;
	dma_receipt.loader_lifecycle = test_instance.lifecycle;
	dma_receipt.handoff = (struct starbook_mtl_dma_smm_range) {
		.base = 0x100000, .size = 0x1000,
	};
	dma_receipt.tables = (struct starbook_mtl_dma_smm_range) {
		.base = 0x200000, .size = 0x1000,
	};
	dma_receipt.table_mirror = (struct starbook_mtl_dma_smm_range) {
		.base = 0x300000, .size = 0x1000,
	};
	for (size_t index = 0; index < ARRAY_SIZE(dma_receipt.arenas); index++) {
		dma_receipt.arenas[index].base = 0x400000 + index * 0x10000;
		dma_receipt.arenas[index].size = 0x1000;
	}
	test_frame.request.backing_base = 0x900000;
	test_frame.request.backing_size =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE;
	test_frame.request.backing_receipt.generation = 73;
	test_frame.request.backing_receipt.boot_kind =
		BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT;
	test_frame.request.backing_receipt.base = test_frame.request.backing_base;
	test_frame.request.backing_receipt.bytes = test_frame.request.backing_size;
	test_frame.request.backing_receipt.tag = BM_MEM_RESERVED;
	test_frame.request.backing_receipt.use =
		BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE;
	dma_memory.frame.base = (uintptr_t)&test_frame;
	dma_memory.frame.size = sizeof(test_frame);
}

static const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
*install(void)
{
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies = NULL;

	assert(starbook_mtl_authvar_presence_lifecycle_close_install_policy(
		&dependencies, &test_active_ops) == CB_SUCCESS);
	assert(dependencies && dependencies->active_ops == &test_active_ops);
	assert(dependencies->internal->context == &owner.binding);
	return dependencies;
}

static void prove(const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
	*dependencies)
{
	assert(dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame, sizeof(test_frame)));
	assert(verifier_calls == 1);
	assert(owner.context.proof_state == PROOF_READY);
}

static void expect_claim_poison(struct install_policy_binding *binding,
	uint64_t source)
{
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fail_stop_expected = true;
	if (!setjmp(fail_stop_jump)) {
		claim(binding, source, &invocation);
		assert(false);
	}
	fail_stop_expected = false;
	assert(owner.context.proof_state == PROOF_POISONED);
	assert(owner.context.claim_state == CLAIM_POISONED);
}

static void test_one_shot_proof_and_copied_route_context(void)
{
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies;
	struct install_policy_binding copied;
	struct payload_mm_authvar_presence_transaction_invocation invocation;
	unsigned int calls;

	fixture();
	dependencies = install();
	prove(dependencies);
	calls = dma_binding_calls;
	assert(dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame, sizeof(test_frame)));
	assert(verifier_calls == 1);
	assert(dma_binding_calls > calls);
	copied = owner.binding;
	assert(dependencies->internal->claim(&copied,
		PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM,
		&invocation) == CB_SUCCESS);
	assert(dependencies->internal->complete(&copied, &invocation, 1) ==
		CB_SUCCESS);
	assert(dependencies->internal->claim(&copied,
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RESUME_CLAIM,
		&invocation) == CB_SUCCESS);
	assert(dependencies->internal->dma_protected(&copied,
		test_frame.request.backing_base, test_frame.request.backing_size));
	assert(dependencies->internal->complete(&copied, &invocation, 1) ==
		CB_SUCCESS);
	assert(owner.context.consumed_sources == 3U);
}

static void test_proof_failures_are_terminal(void)
{
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies;

	fixture();
	dependencies = install();
	mutate_during_verify = true;
	assert(!dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame, sizeof(test_frame)));
	assert(owner.context.proof_state == PROOF_POISONED);
	mutate_during_verify = false;
	assert(!dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame, sizeof(test_frame)));
	assert(verifier_calls == 1);

	fixture();
	dependencies = install();
	verify_ok = false;
	assert(!dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame, sizeof(test_frame)));
	assert(owner.context.proof_state == PROOF_POISONED);
	assert(verifier_calls == 1);

	fixture();
	dependencies = install();
	prove(dependencies);
	test_frame.request.backing_base++;
	assert(!dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame, sizeof(test_frame)));
	assert(owner.context.proof_state == PROOF_POISONED);
	assert(verifier_calls == 1);
}

static void test_boundaries_overlap_and_stale_identity(void)
{
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies;
	struct install_policy_binding copied;
	uint64_t saved;

	fixture();
	dependencies = install();
	assert(!dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame + 1U, sizeof(test_frame)));
	assert(!dependencies->communication_range_valid(
		dependencies->communication_range_context,
		(uintptr_t)&test_frame, sizeof(test_frame) - 1U));
	prove(dependencies);
	copied = owner.binding;
	assert(!dependencies->internal->dma_protected(&copied,
		owner.context.backing_base + 1U, owner.context.backing_size));
	saved = dma_receipt.tables.base;
	dma_receipt.tables.base = owner.context.backing_base;
	assert(!dependencies->internal->dma_protected(&copied,
		owner.context.backing_base, owner.context.backing_size));
	dma_receipt.tables.base = saved;
	test_instance.loader_instance_nonce.low++;
	expect_claim_poison(&copied,
		PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM);
}

static void test_reentry_duplicate_and_completion_mismatch(void)
{
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies;
	struct install_policy_binding copied;
	struct payload_mm_authvar_presence_transaction_invocation invocation;

	fixture();
	dependencies = install();
	prove(dependencies);
	copied = owner.binding;
	assert(claim(&copied, PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM,
		&invocation) == CB_SUCCESS);
	expect_claim_poison(&copied,
		PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RESUME_CLAIM);

	fixture();
	dependencies = install();
	prove(dependencies);
	copied = owner.binding;
	assert(claim(&copied, PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM,
		&invocation) == CB_SUCCESS);
	invocation.smi_generation++;
	fail_stop_expected = true;
	if (!setjmp(fail_stop_jump)) {
		complete(&copied, &invocation, 1);
		assert(false);
	}
	fail_stop_expected = false;
	assert(owner.context.claim_state == CLAIM_POISONED);
}

struct install_thread_result {
	uint32_t ready;
	enum cb_err status;
};

static void *install_thread(void *opaque)
{
	struct install_thread_result *result = opaque;
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies;

	while (!__atomic_load_n(&result->ready, __ATOMIC_ACQUIRE))
		;
	result->status = starbook_mtl_authvar_presence_lifecycle_close_install_policy(
		&dependencies, &test_active_ops);
	return NULL;
}

static void test_exact_one_owner_concurrent_install(void)
{
	pthread_t threads[2];
	struct install_thread_result results[2] = { 0 };
	unsigned int successes = 0;

	fixture();
	for (size_t index = 0; index < ARRAY_SIZE(threads); index++)
		assert(!pthread_create(&threads[index], NULL, install_thread,
			&results[index]));
	for (size_t index = 0; index < ARRAY_SIZE(results); index++)
		__atomic_store_n(&results[index].ready, 1U, __ATOMIC_RELEASE);
	for (size_t index = 0; index < ARRAY_SIZE(threads); index++) {
		assert(!pthread_join(threads[index], NULL));
		successes += results[index].status == CB_SUCCESS;
	}
	assert(successes == 1U);
}

static void test_s3_reload_install_is_rejected(void)
{
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies = (const void *)(uintptr_t)1;

	fixture();
	test_instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	dma_receipt.loader_lifecycle = test_instance.lifecycle;
	assert(starbook_mtl_authvar_presence_lifecycle_close_install_policy(
		&dependencies, &test_active_ops) == CB_ERR);
	assert(!dependencies);
	assert(owner.state == POLICY_FAILED);
}

int main(void)
{
	test_one_shot_proof_and_copied_route_context();
	test_proof_failures_are_terminal();
	test_boundaries_overlap_and_stale_identity();
	test_reentry_duplicate_and_completion_mismatch();
	test_exact_one_owner_concurrent_install();
	test_s3_reload_install_is_rejected();
	return 0;
}
