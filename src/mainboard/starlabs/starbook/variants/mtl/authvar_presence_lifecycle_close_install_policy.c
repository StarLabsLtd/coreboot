/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_lifecycle_close_install.h"
#include "dma_smm_receipt_provision.h"

#include <boot/payload_mm_authvar_presence_lifecycle_close_backing.h>
#include <bootmem.h>
#include <commonlib/helpers.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <string.h>

#include "../../../../../lib/payload_mm_authvar_presence_lifecycle_close_claims.h"

#if !ENV_SMM && !ENV_TEST
#error "StarBook MTL lifecycle-close install policy is SMM-only"
#endif

#define INSTALL_POLICY_IDENTITY 0x4d544c434c504f4cULL
#define INSTALL_POLICY_BINDING_IDENTITY 0x4d544c434c424e44ULL

enum policy_state { POLICY_EMPTY, POLICY_PREPARING, POLICY_READY,
	POLICY_FAILED };
enum proof_state { PROOF_EMPTY, PROOF_VERIFYING, PROOF_READY, PROOF_POISONED };
enum claim_state { CLAIM_IDLE, CLAIM_ACTIVE, CLAIM_POISONED };

struct install_policy_context {
	uint64_t identity;
	uint64_t authority_generation;
	uint64_t backing_base;
	uint64_t backing_size;
	uint64_t next_generation;
	uint64_t active_generation;
	uint64_t active_claim;
	uint64_t active_proof[3];
	struct smm_invocation_loader_instance_nonce loader_nonce;
	uint32_t loader_lifecycle;
	uint32_t initiator_cpu;
	uint32_t active_cpus;
	uint32_t proof_state;
	uint32_t claim_state;
	uint32_t consumed_sources;
	uint32_t reserved;
};

struct install_policy_binding {
	uint64_t identity;
	struct install_policy_context *state;
};

struct install_policy_owner {
	uint32_t state;
	uint32_t reserved;
	struct install_policy_context context;
	struct install_policy_binding binding;
	struct payload_mm_authvar_presence_lifecycle_close_internal_policy internal;
	struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		dependencies;
};

static struct install_policy_owner owner __aligned(8);

_Static_assert(sizeof(struct install_policy_context) <=
	PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_CONTEXT_MAX,
	"MTL lifecycle-close install context is too large");

static bool overlaps(uint64_t first_base, uint64_t first_size,
	uint64_t second_base, uint64_t second_size)
{
	if (!first_base || !first_size || !second_base || !second_size ||
	    first_base > UINT64_MAX - (first_size - 1U) ||
	    second_base > UINT64_MAX - (second_size - 1U))
		return true;
	return first_base <= second_base ?
		second_base - first_base < first_size :
		first_base - second_base < second_size;
}

static bool protected_storage(void *unused, const void *object, size_t size)
{
	const struct smm_invocation_runtime_view *view;

	(void)unused;
	return smm_invocation_runtime_view_get(&view) == CB_SUCCESS &&
		smm_invocation_runtime_range_is_protected(view, object, size) ==
			CB_SUCCESS;
}

static bool receipt_excludes(const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t base, uint64_t size)
{
	const struct starbook_mtl_dma_smm_range *const ranges[] = {
		&receipt->handoff, &receipt->tables, &receipt->table_mirror,
		&receipt->arenas[0], &receipt->arenas[1], &receipt->arenas[2],
	};

	for (size_t index = 0; index < ARRAY_SIZE(ranges); index++)
		if (overlaps(base, size, ranges[index]->base, ranges[index]->size))
			return false;
	return true;
}

static bool state_valid(const struct install_policy_context *context)
{
	return context && context->identity == INSTALL_POLICY_IDENTITY &&
		context->authority_generation && context->backing_base &&
		context->backing_size ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
		context->next_generation &&
		!smm_invocation_loader_instance_nonce_is_zero(
			context->loader_nonce) && context->active_cpus &&
		context->initiator_cpu < context->active_cpus && !context->reserved;
}

static struct install_policy_context *binding_state(void *opaque)
{
	const struct install_policy_binding *binding = opaque;

	if (!binding || binding->identity != INSTALL_POLICY_BINDING_IDENTITY ||
	    binding->state != &owner.context ||
	    !protected_storage(NULL, binding, sizeof(*binding)) ||
	    !protected_storage(NULL, binding->state, sizeof(*binding->state)))
		return NULL;
	return binding->state;
}

static bool dma_backing_valid(const struct install_policy_context *context,
	uint64_t base, uint64_t size)
{
	struct starbook_mtl_dma_smm_binding binding;
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;

	if (!state_valid(context) || base != context->backing_base ||
	    size != context->backing_size ||
	    starbook_mtl_dma_smm_binding_get(&binding) != CB_SUCCESS ||
	    !binding.receipt ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_loader_instance_read(runtime.instance, &instance) !=
		CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS)
		return false;
	return smm_invocation_loader_instance_nonce_equal(
			instance.loader_instance_nonce, context->loader_nonce) &&
		smm_invocation_loader_instance_nonce_equal(
			binding.receipt->loader_instance_nonce,
			context->loader_nonce) &&
		instance.lifecycle == context->loader_lifecycle &&
		binding.receipt->loader_lifecycle == context->loader_lifecycle &&
		topology.bsp_cpu == context->initiator_cpu &&
		topology.active_cpus == context->active_cpus &&
		receipt_excludes(binding.receipt, base, size);
}

static void __noreturn poison(struct install_policy_context *context)
{
	if (context) {
		__atomic_store_n(&context->proof_state, PROOF_POISONED,
			__ATOMIC_RELEASE);
		__atomic_store_n(&context->claim_state, CLAIM_POISONED,
			__ATOMIC_RELEASE);
	}
	platform_payload_mm_authvar_presence_lifecycle_close_route_fail_stop();
}

static bool communication_range_valid(void *opaque, uint64_t base,
	uint64_t size)
{
	struct install_policy_binding *binding = opaque;
	struct install_policy_context *context;
	struct payload_mm_authvar_presence_lifecycle_close_install_frame *frame;
	struct payload_mm_authvar_presence_lifecycle_close_install_frame snapshot;
	struct bootmem_reservation_receipt_authority *verifier;
	struct bootmem_reservation_receipt receipt;
	const struct smm_dma_receipt_memory *memory;
	uint32_t expected;

	if (!binding || binding != &owner.binding ||
	    (context = binding_state(binding)) == NULL ||
	    !smm_get_dma_receipt_memory(&memory) || base != memory->frame.base ||
	    size != sizeof(*frame) || size > memory->frame.size)
		return false;
	frame = (void *)(uintptr_t)base;
	expected = PROOF_EMPTY;
	if (__atomic_compare_exchange_n(&context->proof_state, &expected,
		PROOF_VERIFYING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		snapshot = *frame;
		verifier =
			smm_get_payload_mm_authvar_presence_lifecycle_close_backing_verifier();
		receipt = snapshot.request.backing_receipt;
		if (!protected_storage(NULL, verifier, sizeof(*verifier)) ||
		    bootmem_reservation_receipt_verify_consume_exact_tag(verifier,
			&receipt, BM_MEM_RESERVED) != CB_SUCCESS ||
		    memcmp(frame, &snapshot, sizeof(snapshot)) ||
		    snapshot.request.backing_receipt.base !=
			snapshot.request.backing_base ||
		    snapshot.request.backing_receipt.bytes !=
			snapshot.request.backing_size ||
		    snapshot.request.backing_receipt.tag != BM_MEM_RESERVED ||
		    snapshot.request.backing_receipt.use !=
			BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE) {
			__atomic_store_n(&context->proof_state, PROOF_POISONED,
				__ATOMIC_RELEASE);
			memset(&snapshot, 0, sizeof(snapshot));
			return false;
		}
		context->authority_generation =
			snapshot.request.backing_receipt.generation;
		context->backing_base = snapshot.request.backing_base;
		context->backing_size = snapshot.request.backing_size;
		if (!dma_backing_valid(context, context->backing_base,
			context->backing_size)) {
			__atomic_store_n(&context->proof_state, PROOF_POISONED,
				__ATOMIC_RELEASE);
			memset(&snapshot, 0, sizeof(snapshot));
			return false;
		}
		memset(&snapshot, 0, sizeof(snapshot));
		__atomic_store_n(&context->proof_state, PROOF_READY,
			__ATOMIC_RELEASE);
		return true;
	}
	if (expected != PROOF_READY)
		return false;
	if (!state_valid(context) ||
	    frame->request.backing_receipt.generation !=
		context->authority_generation ||
	    frame->request.backing_base != context->backing_base ||
	    frame->request.backing_size != context->backing_size ||
	    !dma_backing_valid(context, context->backing_base,
		context->backing_size)) {
		__atomic_store_n(&context->proof_state, PROOF_POISONED,
			__ATOMIC_RELEASE);
		return false;
	}
	return true;
}

static uint32_t source_bit(uint64_t claim)
{
	if (claim == PAYLOAD_MM_AUTHVAR_PRESENCE_WARM_RESET_CLAIM)
		return 1U;
	if (claim == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_RESUME_CLAIM)
		return 2U;
	return 0;
}

static void invocation_fill(struct install_policy_context *context,
	uint64_t claim,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	context->active_proof[0] = context->loader_nonce.low ^ claim ^
		context->active_generation;
	context->active_proof[1] = context->loader_nonce.high ^
		(context->active_generation << 17 |
		 context->active_generation >> 47);
	context->active_proof[2] = context->authority_generation ^
		(context->active_generation << 31 |
		 context->active_generation >> 33);
	if (!(context->active_proof[0] | context->active_proof[1] |
	      context->active_proof[2]))
		context->active_proof[0] = INSTALL_POLICY_IDENTITY;
	*invocation =
		(struct payload_mm_authvar_presence_transaction_invocation) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_INVOCATION_REVISION,
			.size = sizeof(*invocation),
			.initiator_cpu = context->initiator_cpu,
			.active_cpus = context->active_cpus,
			.smi_generation = context->active_generation,
			.rendezvous_generation = context->active_generation,
			.rendezvous_proof = { context->active_proof[0],
				context->active_proof[1], context->active_proof[2] },
			.bsp = 1U,
		};
}

static enum cb_err claim(void *opaque, uint64_t claim,
	struct payload_mm_authvar_presence_transaction_invocation *invocation)
{
	struct install_policy_context *context = binding_state(opaque);
	uint32_t bit = source_bit(claim);
	uint32_t expected = CLAIM_IDLE;

	if (invocation)
		memset(invocation, 0, sizeof(*invocation));
	if (!state_valid(context) || !invocation || !bit ||
	    __atomic_load_n(&context->proof_state, __ATOMIC_ACQUIRE) !=
		PROOF_READY || context->consumed_sources & bit ||
	    !dma_backing_valid(context, context->backing_base,
		context->backing_size) ||
	    !__atomic_compare_exchange_n(&context->claim_state, &expected,
		CLAIM_ACTIVE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		poison(context);
	if (context->next_generation == UINT64_MAX)
		poison(context);
	context->active_generation = ++context->next_generation;
	context->active_claim = claim;
	invocation_fill(context, claim, invocation);
	if (!dma_backing_valid(context, context->backing_base,
		context->backing_size))
		poison(context);
	return CB_SUCCESS;
}

static enum cb_err complete(void *opaque,
	const struct payload_mm_authvar_presence_transaction_invocation *invocation,
	uint64_t value)
{
	struct install_policy_context *context = binding_state(opaque);
	struct payload_mm_authvar_presence_transaction_invocation expected;
	uint32_t bit;

	if (!state_valid(context) || !invocation || !value ||
	    __atomic_load_n(&context->claim_state, __ATOMIC_ACQUIRE) != CLAIM_ACTIVE)
		poison(context);
	bit = source_bit(context->active_claim);
	invocation_fill(context, context->active_claim, &expected);
	if (!bit || context->consumed_sources & bit ||
	    memcmp(invocation, &expected, sizeof(expected)) ||
	    !dma_backing_valid(context, context->backing_base,
		context->backing_size))
		poison(context);
	context->consumed_sources |= bit;
	context->active_generation = 0;
	context->active_claim = 0;
	memset(context->active_proof, 0, sizeof(context->active_proof));
	__atomic_store_n(&context->claim_state, CLAIM_IDLE, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

static bool dma_protected(void *opaque, uint64_t base, uint64_t size)
{
	const struct install_policy_context *context = binding_state(opaque);

	return state_valid(context) &&
		__atomic_load_n(&context->proof_state, __ATOMIC_ACQUIRE) ==
		PROOF_READY && dma_backing_valid(context, base, size);
}

enum cb_err starbook_mtl_authvar_presence_lifecycle_close_install_policy(
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		**dependencies,
	const struct smm_invocation_save_state_ops *active_ops)
{
	struct smm_invocation_runtime_binding runtime;
	struct smm_invocation_loader_instance instance;
	struct smm_invocation_topology topology;
	struct starbook_mtl_dma_smm_binding dma;
	uint32_t expected = POLICY_EMPTY;

	if (dependencies)
		*dependencies = NULL;
	if (!dependencies || !active_ops ||
	    !__atomic_compare_exchange_n(&owner.state, &expected,
		POLICY_PREPARING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (!protected_storage(NULL, &owner, sizeof(owner)) ||
	    !protected_storage(NULL, active_ops, sizeof(*active_ops)) ||
	    smm_invocation_runtime_binding_get(&runtime) != CB_SUCCESS ||
	    smm_invocation_loader_instance_read(runtime.instance, &instance) !=
		CB_SUCCESS ||
	    smm_invocation_topology_read(runtime.topology, &topology) != CB_SUCCESS ||
	    starbook_mtl_dma_smm_binding_get(&dma) != CB_SUCCESS || !dma.receipt ||
	    instance.lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD ||
	    instance.lifecycle != dma.receipt->loader_lifecycle ||
	    !smm_invocation_loader_instance_nonce_equal(
		instance.loader_instance_nonce,
		dma.receipt->loader_instance_nonce) ||
	    !runtime.evidence->closed_generation ||
	    runtime.evidence->closed_generation == UINT64_MAX ||
	    !topology.active_cpus || topology.bsp_cpu >= topology.active_cpus)
		goto fail;
	owner.context = (struct install_policy_context) {
		.identity = INSTALL_POLICY_IDENTITY,
		.next_generation = runtime.evidence->closed_generation,
		.loader_nonce = instance.loader_instance_nonce,
		.loader_lifecycle = instance.lifecycle,
		.initiator_cpu = topology.bsp_cpu,
		.active_cpus = topology.active_cpus,
		.proof_state = PROOF_EMPTY,
		.claim_state = CLAIM_IDLE,
	};
	owner.binding = (struct install_policy_binding) {
		.identity = INSTALL_POLICY_BINDING_IDENTITY,
		.state = &owner.context,
	};
	owner.internal =
		(struct payload_mm_authvar_presence_lifecycle_close_internal_policy) {
			.revision =
				PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_ROUTE_POLICY_REVISION,
			.size = sizeof(owner.internal),
			.claim = claim,
			.complete = complete,
			.dma_protected = dma_protected,
			.context = &owner.binding,
			.context_size = sizeof(owner.binding),
		};
	owner.dependencies =
		(struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies) {
			.internal = &owner.internal,
			.active_ops = active_ops,
			.protected_storage = protected_storage,
			.communication_range_valid = communication_range_valid,
			.communication_range_context = &owner.binding,
			.communication_range_context_size = sizeof(owner.binding),
		};
	if (!protected_storage(NULL, &owner, sizeof(owner)))
		goto fail;
	__atomic_store_n(&owner.state, POLICY_READY, __ATOMIC_RELEASE);
	*dependencies = &owner.dependencies;
	return CB_SUCCESS;
fail:
	__atomic_store_n(&owner.state, POLICY_FAILED, __ATOMIC_RELEASE);
	return CB_ERR;
}

#if ENV_TEST
void starbook_mtl_authvar_presence_lifecycle_close_install_policy_reset_test(void)
{
	memset(&owner, 0, sizeof(owner));
}
#endif
