/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_clear_x86.h"

#include "mor_live_inventory.h"

#include <commonlib/helpers.h>
#include <string.h>

#define MTL_MOR_CLEAR_LIMIT_EXCLUSIVE (1ULL << 32)
#define MTL_MOR_CLEAR_AUTHORITY_DOMAIN 0x6d746c2d6d6f7265ULL

enum mtl_mor_clear_phase {
	MTL_MOR_CLEAR_EMPTY,
	MTL_MOR_CLEAR_BOUND,
	MTL_MOR_CLEAR_INVENTORY_BEFORE,
	MTL_MOR_CLEAR_DMA_BEFORE,
	MTL_MOR_CLEAR_DMA_AFTER,
	MTL_MOR_CLEAR_COMPLETE,
	MTL_MOR_CLEAR_BUSY,
	MTL_MOR_CLEAR_POISONED,
};

struct mtl_mor_clear_lifecycle {
	uintptr_t owner;
	uint64_t generation;
	enum mtl_mor_clear_phase phase;
};

enum mtl_mor_clear_scratch_owner {
	MTL_MOR_CLEAR_SCRATCH_IDLE,
	MTL_MOR_CLEAR_SCRATCH_PREPARE,
	MTL_MOR_CLEAR_SCRATCH_INVENTORY,
	MTL_MOR_CLEAR_SCRATCH_DMA,
	MTL_MOR_CLEAR_SCRATCH_POISONED,
};

struct mtl_mor_clear_guard_workspace {
	struct starbook_mtl_dma_guard_snapshot bound;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma;
	struct starbook_mtl_mor_clear_x86_authority recheck;
	struct starbook_mtl_dma_guard_bind_workspace bind;
};

struct mtl_mor_clear_prepare_workspace {
	struct starbook_mtl_mor_clear_x86_reservations reservations;
	struct starbook_mtl_dma_guard_snapshot guard;
	struct bootmem_aligned_reservation page_tables;
	struct bootmem_aligned_reservation aperture;
	struct bootmem_aligned_reservation transport;
	struct payload_mm_authvar_mor_live_inventory_overlay overlays[
		STARBOOK_MTL_MOR_CLEAR_X86_OVERLAYS];
	struct payload_mm_authvar_mor_clear_plan candidate;
	struct starbook_mtl_dma_guard_snapshot bound;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma;
	struct starbook_mtl_mor_clear_x86_authority authority;
	struct starbook_mtl_mor_live_inventory_workspace inventory;
	struct starbook_mtl_dma_guard_bind_workspace bind;
};

struct mtl_mor_clear_inventory_workspace {
	struct starbook_mtl_mor_clear_x86_authority authority;
	struct starbook_mtl_mor_clear_x86_authority recheck;
	struct payload_mm_authvar_mor_clear_plan expected;
	struct mtl_mor_clear_guard_workspace guard;
	struct starbook_mtl_mor_live_inventory_workspace inventory;
};

static struct {
	union {
		struct mtl_mor_clear_prepare_workspace prepare;
		struct mtl_mor_clear_inventory_workspace inventory;
		struct {
			struct starbook_mtl_mor_clear_x86_authority authority;
			struct mtl_mor_clear_guard_workspace guard;
		} dma;
	} work;
	struct mtl_mor_clear_lifecycle lifecycle;
	uint32_t owner;
} clear_scratch;

static enum cb_err executor_dma_snapshot(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot);
static enum cb_err executor_inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan);

static const struct bootmem_aligned_reservation_request requests[] = {
	{
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(struct bootmem_aligned_reservation_request),
		.bytes = PAE_PGTL_SIZE,
		.alignment = PAE_PGTL_ALIGN,
		.limit_exclusive = MTL_MOR_CLEAR_LIMIT_EXCLUSIVE,
		.tag = BM_MEM_TABLE,
	},
	{
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(struct bootmem_aligned_reservation_request),
		.bytes = PAE_VMEM_SIZE,
		.alignment = PAE_VMEM_ALIGN,
		.limit_exclusive = MTL_MOR_CLEAR_LIMIT_EXCLUSIVE,
		.tag = BM_MEM_RESERVED,
	},
};

static bool bytes_zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return !combined;
}

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(uint64_t first_base, uint64_t first_size,
	uint64_t second_base, uint64_t second_size)
{
	if (!first_size || !second_size || first_base > UINT64_MAX - first_size ||
	    second_base > UINT64_MAX - second_size)
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool disjoint_from_scratch(const void *object, size_t size)
{
	return object_valid(object, size, 1) &&
		!ranges_overlap((uintptr_t)object, size, (uintptr_t)&clear_scratch,
			sizeof(clear_scratch));
}

static void *scratch_claim(enum mtl_mor_clear_scratch_owner owner)
{
	uint32_t expected = MTL_MOR_CLEAR_SCRATCH_IDLE;

	if (!__atomic_compare_exchange_n(&clear_scratch.owner, &expected, owner,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&clear_scratch.owner,
			MTL_MOR_CLEAR_SCRATCH_POISONED, __ATOMIC_RELEASE);
		return NULL;
	}
	memset(&clear_scratch.work, 0, sizeof(clear_scratch.work));
	return &clear_scratch.work;
}

static bool scratch_owned(enum mtl_mor_clear_scratch_owner owner)
{
	return __atomic_load_n(&clear_scratch.owner, __ATOMIC_ACQUIRE) == owner;
}

static bool scratch_release(enum mtl_mor_clear_scratch_owner owner)
{
	uint32_t expected = owner;

	scrub(&clear_scratch.work, sizeof(clear_scratch.work));
	if (__atomic_compare_exchange_n(&clear_scratch.owner, &expected,
		MTL_MOR_CLEAR_SCRATCH_IDLE, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return true;
	__atomic_store_n(&clear_scratch.owner, MTL_MOR_CLEAR_SCRATCH_POISONED,
		__ATOMIC_RELEASE);
	return false;
}

static void scratch_abort(enum mtl_mor_clear_scratch_owner owner)
{
	const uint32_t current = __atomic_load_n(&clear_scratch.owner,
		__ATOMIC_ACQUIRE);

	if (current == owner || current == MTL_MOR_CLEAR_SCRATCH_POISONED)
		scrub(&clear_scratch.work, sizeof(clear_scratch.work));
	__atomic_store_n(&clear_scratch.owner, MTL_MOR_CLEAR_SCRATCH_POISONED,
		__ATOMIC_RELEASE);
}

static uint64_t mix_bytes(const void *buffer, size_t size, uint64_t value)
{
	const uint8_t *bytes = buffer;

	for (size_t index = 0; index < size; index++) {
		value ^= bytes[index];
		value *= 0x100000001b3ULL;
		value ^= value >> 32;
	}
	return value;
}

static uint64_t authority_seal(
	const struct starbook_mtl_mor_clear_x86_authority *authority)
{
	return mix_bytes(authority,
		offsetof(struct starbook_mtl_mor_clear_x86_authority, seal),
		MTL_MOR_CLEAR_AUTHORITY_DOMAIN);
}

static struct starbook_mtl_mor_clear_x86_binding *binding_from_context(
	void *context)
{
	const enum mtl_mor_clear_phase phase = __atomic_load_n(
		&clear_scratch.lifecycle.phase, __ATOMIC_ACQUIRE);

	if (!object_valid(context,
		sizeof(struct starbook_mtl_mor_clear_x86_binding),
		_Alignof(struct starbook_mtl_mor_clear_x86_binding)) ||
	    (uintptr_t)context != __atomic_load_n(&clear_scratch.lifecycle.owner,
		__ATOMIC_ACQUIRE) || phase == MTL_MOR_CLEAR_EMPTY ||
	    phase == MTL_MOR_CLEAR_BUSY || phase == MTL_MOR_CLEAR_POISONED)
		return NULL;
	return context;
}

static bool lifecycle_claim(struct starbook_mtl_mor_clear_x86_binding *binding,
	uint64_t generation)
{
	enum mtl_mor_clear_phase expected = MTL_MOR_CLEAR_EMPTY;

	if (!generation ||
	    !__atomic_compare_exchange_n(&clear_scratch.lifecycle.phase, &expected,
		MTL_MOR_CLEAR_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	__atomic_store_n(&clear_scratch.lifecycle.owner, (uintptr_t)binding,
		__ATOMIC_RELEASE);
	__atomic_store_n(&clear_scratch.lifecycle.generation, generation,
		__ATOMIC_RELEASE);
	expected = MTL_MOR_CLEAR_BUSY;
	return __atomic_compare_exchange_n(&clear_scratch.lifecycle.phase, &expected,
		MTL_MOR_CLEAR_BOUND, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static bool lifecycle_advance(
	struct starbook_mtl_mor_clear_x86_binding *binding,
	uint64_t generation, enum mtl_mor_clear_phase expected,
	enum mtl_mor_clear_phase next)
{
	enum mtl_mor_clear_phase current = expected;

	if (__atomic_load_n(&clear_scratch.lifecycle.owner, __ATOMIC_ACQUIRE) !=
		(uintptr_t)binding ||
	    __atomic_load_n(&clear_scratch.lifecycle.generation, __ATOMIC_ACQUIRE) !=
		generation ||
	    __atomic_load_n(&binding->phase, __ATOMIC_ACQUIRE) != expected ||
	    !__atomic_compare_exchange_n(&clear_scratch.lifecycle.phase, &current,
		next, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return false;
	__atomic_store_n(&binding->phase, MTL_MOR_CLEAR_BUSY, __ATOMIC_RELEASE);
	return true;
}

static bool authority_fields_valid(
	const struct starbook_mtl_mor_clear_x86_binding *binding,
	const struct starbook_mtl_mor_clear_x86_authority *authority)
{
	const struct payload_mm_authvar_mor_clear_executor_ops *ops = &binding->ops;

	return authority->revision == STARBOOK_MTL_MOR_CLEAR_X86_REVISION &&
		authority->size == sizeof(*authority) &&
		authority->owner == (uintptr_t)binding &&
		object_valid((const void *)authority->plan_address,
			sizeof(authority->plan), _Alignof(authority->plan)) &&
		!ranges_overlap(authority->plan_address, sizeof(authority->plan),
			(uintptr_t)binding, sizeof(*binding)) &&
		authority->page_tables == binding->backend.page_tables &&
		authority->aperture == binding->backend.aperture &&
		authority->transport.size ==
			STARBOOK_MTL_MOR_CLEAR_X86_TRANSPORT_SIZE &&
		authority->transport.tag == BM_MEM_TABLE &&
		!authority->transport.reserved &&
		!(authority->transport.base %
			STARBOOK_MTL_MOR_CLEAR_X86_TRANSPORT_SIZE) &&
		authority->transport.base <=
			UINTPTR_MAX - (authority->transport.size - 1U) &&
		authority->prepared.generation &&
		authority->prepared.generation == authority->bound.generation &&
		authority->prepared.generation == authority->dma.generation &&
		authority->prepared.generation ==
			authority->plan.inventory_generation &&
		!memcmp(authority->prepared.identity, authority->bound.identity,
			sizeof(authority->prepared.identity)) &&
		!memcmp(authority->prepared.identity, authority->dma.identity,
			sizeof(authority->prepared.identity)) &&
		!memcmp(authority->prepared.identity,
			authority->plan.inventory_identity,
			sizeof(authority->prepared.identity)) &&
		payload_mm_authvar_mor_clear_plan_validate(&authority->plan) ==
			CB_SUCCESS &&
		!memcmp(&authority->backend, &binding->backend,
			sizeof(authority->backend)) &&
		!memcmp(&authority->ops, ops, sizeof(authority->ops)) &&
		ops->context == &binding->backend &&
		ops->context_size == sizeof(binding->backend) &&
		ops->inventory_context == &binding->backend &&
		ops->inventory_context_size == sizeof(binding->backend) &&
		object_valid(ops->executable_owner, ops->executable_owner_size, 1) &&
		object_valid(ops->stack_owner, ops->stack_owner_size, 1) &&
		ops->dma_snapshot == executor_dma_snapshot &&
		ops->inventory_validate == executor_inventory_validate &&
		authority->overlays[0].base == authority->page_tables &&
		authority->overlays[0].size == PAE_PGTL_SIZE &&
		authority->overlays[0].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[0].reserved &&
		authority->overlays[1].base == authority->aperture &&
		authority->overlays[1].size == PAE_VMEM_SIZE &&
		authority->overlays[1].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED &&
		!authority->overlays[1].reserved &&
		authority->overlays[2].base == authority->plan_address &&
		authority->overlays[2].size == sizeof(authority->plan) &&
		authority->overlays[2].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[2].reserved &&
		authority->overlays[3].base == (uintptr_t)binding &&
		authority->overlays[3].size == sizeof(*binding) &&
		authority->overlays[3].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[3].reserved &&
		authority->overlays[4].base == (uintptr_t)&clear_scratch &&
		authority->overlays[4].size == sizeof(clear_scratch) &&
		authority->overlays[4].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[4].reserved &&
		authority->overlays[5].base == authority->transport.base &&
		authority->overlays[5].size == authority->transport.size &&
		authority->overlays[5].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE &&
		!authority->overlays[5].reserved;
}

static bool authority_capture(
	struct starbook_mtl_mor_clear_x86_binding *binding,
	enum mtl_mor_clear_phase phase,
	struct starbook_mtl_mor_clear_x86_authority *authority)
{
	if (!object_valid(binding, sizeof(*binding), _Alignof(*binding)) ||
	    __atomic_load_n(&binding->phase, __ATOMIC_ACQUIRE) != phase ||
	    binding->reserved ||
	    memcmp(&binding->authority, &binding->authority_mirror,
		sizeof(binding->authority)))
		return false;
	memcpy(authority, &binding->authority, sizeof(*authority));
	return authority->seal == authority_seal(authority) &&
		authority_fields_valid(binding, authority) &&
		!memcmp(authority, &binding->authority, sizeof(*authority)) &&
		!memcmp(authority, &binding->authority_mirror,
			sizeof(*authority)) &&
		__atomic_load_n(&binding->phase, __ATOMIC_ACQUIRE) == phase &&
		!binding->reserved;
}

static enum cb_err poison_binding(
	struct starbook_mtl_mor_clear_x86_binding *binding)
{
	__atomic_store_n(&clear_scratch.lifecycle.phase, MTL_MOR_CLEAR_POISONED,
		__ATOMIC_RELEASE);
	if (binding &&
	    __atomic_load_n(&clear_scratch.lifecycle.owner, __ATOMIC_ACQUIRE) ==
		(uintptr_t)binding)
		__atomic_store_n(&binding->phase, MTL_MOR_CLEAR_POISONED,
			__ATOMIC_RELEASE);
	return CB_ERR;
}

static bool guard_revalidate(
	struct starbook_mtl_mor_clear_x86_binding *binding,
	enum mtl_mor_clear_phase phase,
	const struct starbook_mtl_mor_clear_x86_authority *authority,
	struct mtl_mor_clear_guard_workspace *workspace)
{
	memset(workspace, 0, sizeof(*workspace));
	if (starbook_mtl_dma_guard_bind_owned(&authority->plan,
		&authority->prepared, &workspace->bound, &workspace->dma,
		&workspace->bind) != CB_SUCCESS ||
	    !authority_capture(binding, phase, &workspace->recheck) ||
	    memcmp(authority, &workspace->recheck, sizeof(workspace->recheck)) ||
	    memcmp(&workspace->bound, &authority->bound,
		sizeof(workspace->bound)) ||
	    memcmp(&workspace->dma, &authority->dma, sizeof(workspace->dma)))
		return false;
	return true;
}

static enum cb_err executor_dma_snapshot(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	struct starbook_mtl_mor_clear_x86_binding *binding =
		binding_from_context(context);
	typeof(clear_scratch.work.dma) *workspace;
	struct payload_mm_authvar_mor_clear_dma_snapshot original;
	enum mtl_mor_clear_phase phase;
	enum mtl_mor_clear_phase next;
	enum cb_err status = CB_ERR;
	bool original_captured = false;

	if (!object_valid(snapshot, sizeof(*snapshot), _Alignof(*snapshot)))
		return poison_binding(binding);
	if (!binding || ranges_overlap((uintptr_t)snapshot, sizeof(*snapshot),
		(uintptr_t)binding, sizeof(*binding)) ||
	    !disjoint_from_scratch(snapshot, sizeof(*snapshot)) ||
	    !disjoint_from_scratch(binding, sizeof(*binding)))
		return poison_binding(binding);
	workspace = scratch_claim(MTL_MOR_CLEAR_SCRATCH_DMA);
	if (!workspace)
		return CB_ERR;
	phase = __atomic_load_n(&binding->phase, __ATOMIC_ACQUIRE);
	if (phase == MTL_MOR_CLEAR_INVENTORY_BEFORE)
		next = MTL_MOR_CLEAR_DMA_BEFORE;
	else if (phase == MTL_MOR_CLEAR_DMA_BEFORE)
		next = MTL_MOR_CLEAR_DMA_AFTER;
	else
		goto out;
	if (!authority_capture(binding, phase, &workspace->authority) ||
	    ranges_overlap((uintptr_t)snapshot, sizeof(*snapshot),
		workspace->authority.plan_address,
		sizeof(workspace->authority.plan)) ||
	    ranges_overlap((uintptr_t)snapshot, sizeof(*snapshot),
		(uintptr_t)workspace->authority.ops.executable_owner,
		workspace->authority.ops.executable_owner_size))
		goto out;
	memcpy(&original, snapshot, sizeof(original));
	original_captured = true;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!lifecycle_advance(binding, workspace->authority.prepared.generation,
		phase, next))
		goto out;
	if (!guard_revalidate(binding, MTL_MOR_CLEAR_BUSY,
		&workspace->authority, &workspace->guard) ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_DMA))
		goto out;
	*snapshot = workspace->authority.dma;
	__atomic_store_n(&binding->phase, next, __ATOMIC_RELEASE);
	status = CB_SUCCESS;
out:
	if (!scratch_release(MTL_MOR_CLEAR_SCRATCH_DMA))
		status = CB_ERR;
	if (status != CB_SUCCESS) {
		if (original_captured)
			memcpy(snapshot, &original, sizeof(*snapshot));
		return poison_binding(binding);
	}
	return CB_SUCCESS;
}

static enum cb_err executor_inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	struct starbook_mtl_mor_clear_x86_binding *binding =
		binding_from_context(context);
	struct mtl_mor_clear_inventory_workspace *workspace;
	enum mtl_mor_clear_phase phase;
	enum mtl_mor_clear_phase next;
	enum cb_err status = CB_ERR;

	if (!binding)
		return poison_binding(NULL);
	if (!object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !disjoint_from_scratch(plan, sizeof(*plan)) ||
	    !disjoint_from_scratch(binding, sizeof(*binding)))
		return poison_binding(binding);
	workspace = scratch_claim(MTL_MOR_CLEAR_SCRATCH_INVENTORY);
	if (!workspace)
		return CB_ERR;
	phase = __atomic_load_n(&binding->phase, __ATOMIC_ACQUIRE);
	if (phase == MTL_MOR_CLEAR_BOUND)
		next = MTL_MOR_CLEAR_INVENTORY_BEFORE;
	else if (phase == MTL_MOR_CLEAR_DMA_AFTER)
		next = MTL_MOR_CLEAR_COMPLETE;
	else
		goto out;
	if (!authority_capture(binding, phase, &workspace->authority) ||
	    (uintptr_t)plan != workspace->authority.plan_address ||
	    memcmp(plan, &workspace->authority.plan, sizeof(*plan)))
		goto out;
	if (!lifecycle_advance(binding, workspace->authority.prepared.generation,
		phase, next))
		goto out;
	if (starbook_mtl_mor_live_inventory_compose_with_overlays_owned(
		&workspace->authority.prepared, workspace->authority.overlays,
		ARRAY_SIZE(workspace->authority.overlays), &workspace->expected,
		&workspace->inventory) != CB_SUCCESS ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_INVENTORY) ||
	    !authority_capture(binding, MTL_MOR_CLEAR_BUSY,
		&workspace->recheck) ||
	    memcmp(&workspace->authority, &workspace->recheck,
		sizeof(workspace->authority)) ||
	    memcmp(&workspace->expected, &workspace->authority.plan,
		sizeof(workspace->expected)) ||
	    memcmp(plan, &workspace->authority.plan, sizeof(*plan)) ||
	    !guard_revalidate(binding, MTL_MOR_CLEAR_BUSY,
		&workspace->authority, &workspace->guard) ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_INVENTORY))
		goto out;
	__atomic_store_n(&binding->phase, next, __ATOMIC_RELEASE);
	status = CB_SUCCESS;
out:
	if (!scratch_release(MTL_MOR_CLEAR_SCRATCH_INVENTORY))
		status = CB_ERR;
	if (status != CB_SUCCESS)
		return poison_binding(binding);
	return CB_SUCCESS;
}

static bool reservation_valid(
	const struct bootmem_aligned_reservation *reservation,
	const struct bootmem_aligned_reservation_request *request)
{
	return reservation->size == request->bytes &&
		reservation->tag == request->tag && !reservation->reserved &&
		!(reservation->base % request->alignment) &&
		reservation->base <= request->limit_exclusive - request->bytes;
}

static bool reservations_valid(
	const struct starbook_mtl_mor_clear_x86_reservations *reservations)
{
	return reservations->revision == STARBOOK_MTL_MOR_CLEAR_X86_REVISION &&
		reservations->size == sizeof(*reservations) &&
		reservations->registered == 1U && !reservations->reserved;
}

enum cb_err starbook_mtl_mor_clear_x86_register(
	struct starbook_mtl_mor_clear_x86_reservations *reservations)
{
	struct starbook_mtl_mor_clear_x86_reservations candidate = {
		.revision = STARBOOK_MTL_MOR_CLEAR_X86_REVISION,
		.size = sizeof(candidate),
	};

	if (!object_valid(reservations, sizeof(*reservations),
		_Alignof(*reservations)) ||
	    !bytes_zero(reservations, sizeof(*reservations)))
		return CB_ERR_ARG;
	if (bootmem_aligned_reservations_register(requests, ARRAY_SIZE(requests),
		candidate.handles))
		return CB_ERR;
	candidate.registered = 1U;
	*reservations = candidate;
	return CB_SUCCESS;
}

static enum cb_err fail(struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_clear_x86_binding *binding)
{
	if (__atomic_load_n(&clear_scratch.lifecycle.owner, __ATOMIC_ACQUIRE) ==
	    (uintptr_t)binding)
		(void)poison_binding(binding);
	memset(plan, 0, sizeof(*plan));
	memset(binding, 0, sizeof(*binding));
	return CB_ERR;
}

#if ENV_TEST
void starbook_mtl_mor_clear_x86_lifecycle_reset_test(void)
{
	memset(&clear_scratch, 0, sizeof(clear_scratch));
}

bool starbook_mtl_mor_clear_x86_scratch_zero_test(void)
{
	return bytes_zero(&clear_scratch.work, sizeof(clear_scratch.work));
}

bool starbook_mtl_mor_clear_x86_scratch_idle_test(void)
{
	return __atomic_load_n(&clear_scratch.owner, __ATOMIC_ACQUIRE) ==
		MTL_MOR_CLEAR_SCRATCH_IDLE;
}
#endif

enum cb_err starbook_mtl_mor_clear_x86_prepare(
	const struct starbook_mtl_mor_clear_x86_reservations *reservations,
	const struct starbook_mtl_dma_guard_snapshot *dma_guard,
	const struct bootmem_aligned_reservation *transport, bool resume_from_s3,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_clear_x86_binding *binding)
{
	struct mtl_mor_clear_prepare_workspace *workspace;
	enum cb_err status = CB_ERR;
	const bool plan_valid = object_valid(plan, sizeof(*plan), _Alignof(*plan));
	const bool binding_valid = object_valid(binding, sizeof(*binding),
		_Alignof(*binding));

	if (!plan_valid || !binding_valid ||
	    !object_valid(reservations, sizeof(*reservations),
		_Alignof(*reservations)) ||
	    !object_valid(dma_guard, sizeof(*dma_guard), _Alignof(*dma_guard)) ||
	    !object_valid(transport, sizeof(*transport), _Alignof(*transport)) ||
	    !disjoint_from_scratch(plan, sizeof(*plan)) ||
	    !disjoint_from_scratch(binding, sizeof(*binding)) ||
	    !disjoint_from_scratch(reservations, sizeof(*reservations)) ||
	    !disjoint_from_scratch(dma_guard, sizeof(*dma_guard)) ||
	    !disjoint_from_scratch(transport, sizeof(*transport)) ||
	    ranges_overlap((uintptr_t)plan, sizeof(*plan), (uintptr_t)binding,
		sizeof(*binding)) ||
	    ranges_overlap((uintptr_t)plan, sizeof(*plan), (uintptr_t)reservations,
		sizeof(*reservations)) ||
	    ranges_overlap((uintptr_t)plan, sizeof(*plan), (uintptr_t)dma_guard,
		sizeof(*dma_guard)) ||
	    ranges_overlap((uintptr_t)binding, sizeof(*binding),
		(uintptr_t)reservations, sizeof(*reservations)) ||
	    ranges_overlap((uintptr_t)binding, sizeof(*binding),
		(uintptr_t)dma_guard, sizeof(*dma_guard)) ||
	    ranges_overlap((uintptr_t)reservations, sizeof(*reservations),
		(uintptr_t)dma_guard, sizeof(*dma_guard)) ||
	    ranges_overlap((uintptr_t)transport, sizeof(*transport),
		(uintptr_t)plan, sizeof(*plan)) ||
	    ranges_overlap((uintptr_t)transport, sizeof(*transport),
		(uintptr_t)binding, sizeof(*binding)) ||
	    ranges_overlap((uintptr_t)transport, sizeof(*transport),
		(uintptr_t)reservations, sizeof(*reservations)) ||
	    ranges_overlap((uintptr_t)transport, sizeof(*transport),
		(uintptr_t)dma_guard, sizeof(*dma_guard)))
		return CB_ERR_ARG;
	workspace = scratch_claim(MTL_MOR_CLEAR_SCRATCH_PREPARE);
	if (!workspace)
		return CB_ERR;
	if (!bytes_zero(plan, sizeof(*plan)) ||
	    !bytes_zero(binding, sizeof(*binding)))
		goto out;
	if (resume_from_s3)
		goto out;
	memcpy(&workspace->reservations, reservations,
		sizeof(workspace->reservations));
	memcpy(&workspace->guard, dma_guard, sizeof(workspace->guard));
	memcpy(&workspace->transport, transport, sizeof(workspace->transport));
	if (!reservations_valid(&workspace->reservations) ||
	    workspace->transport.size != STARBOOK_MTL_MOR_CLEAR_X86_TRANSPORT_SIZE ||
	    workspace->transport.tag != BM_MEM_TABLE ||
	    workspace->transport.reserved ||
	    (workspace->transport.base %
		STARBOOK_MTL_MOR_CLEAR_X86_TRANSPORT_SIZE) ||
	    workspace->transport.base >
		UINTPTR_MAX - (workspace->transport.size - 1U) ||
	    bootmem_aligned_reservation_query(&workspace->reservations.handles[0],
		&workspace->page_tables) ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_PREPARE) ||
	    bootmem_aligned_reservation_query(&workspace->reservations.handles[1],
		&workspace->aperture) ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_PREPARE) ||
	    !reservation_valid(&workspace->page_tables, &requests[0]) ||
	    !reservation_valid(&workspace->aperture, &requests[1]) ||
	    ranges_overlap(workspace->page_tables.base, workspace->page_tables.size,
		workspace->aperture.base, workspace->aperture.size) ||
	    ranges_overlap(workspace->page_tables.base, workspace->page_tables.size,
		(uintptr_t)plan, sizeof(*plan)) ||
	    ranges_overlap(workspace->page_tables.base, workspace->page_tables.size,
		(uintptr_t)binding, sizeof(*binding)) ||
	    ranges_overlap(workspace->page_tables.base, workspace->page_tables.size,
		(uintptr_t)reservations, sizeof(*reservations)) ||
	    ranges_overlap(workspace->page_tables.base, workspace->page_tables.size,
		(uintptr_t)dma_guard, sizeof(*dma_guard)) ||
	    ranges_overlap(workspace->page_tables.base, workspace->page_tables.size,
		(uintptr_t)&clear_scratch, sizeof(clear_scratch)) ||
	    ranges_overlap(workspace->aperture.base, workspace->aperture.size,
		(uintptr_t)plan, sizeof(*plan)) ||
	    ranges_overlap(workspace->aperture.base, workspace->aperture.size,
		(uintptr_t)binding, sizeof(*binding)) ||
	    ranges_overlap(workspace->aperture.base, workspace->aperture.size,
		(uintptr_t)reservations, sizeof(*reservations)) ||
	    ranges_overlap(workspace->aperture.base, workspace->aperture.size,
		(uintptr_t)dma_guard, sizeof(*dma_guard)) ||
	    ranges_overlap(workspace->aperture.base, workspace->aperture.size,
		(uintptr_t)&clear_scratch, sizeof(clear_scratch)) ||
	    ranges_overlap(workspace->transport.base, workspace->transport.size,
		workspace->page_tables.base, workspace->page_tables.size) ||
	    ranges_overlap(workspace->transport.base, workspace->transport.size,
		workspace->aperture.base, workspace->aperture.size) ||
	    ranges_overlap(workspace->transport.base, workspace->transport.size,
		(uintptr_t)plan, sizeof(*plan)) ||
	    ranges_overlap(workspace->transport.base, workspace->transport.size,
		(uintptr_t)binding, sizeof(*binding)) ||
	    ranges_overlap(workspace->transport.base, workspace->transport.size,
		(uintptr_t)reservations, sizeof(*reservations)) ||
	    ranges_overlap(workspace->transport.base, workspace->transport.size,
		(uintptr_t)dma_guard, sizeof(*dma_guard)) ||
	    ranges_overlap(workspace->transport.base, workspace->transport.size,
		(uintptr_t)&clear_scratch, sizeof(clear_scratch)) ||
	    memcmp(&workspace->reservations, reservations,
		sizeof(workspace->reservations)) ||
	    memcmp(&workspace->transport, transport,
		sizeof(workspace->transport)))
		goto out;
	workspace->overlays[0] =
		(struct payload_mm_authvar_mor_live_inventory_overlay) {
			.base = workspace->page_tables.base,
			.size = workspace->page_tables.size,
			.exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		};
	workspace->overlays[1] =
		(struct payload_mm_authvar_mor_live_inventory_overlay) {
			.base = workspace->aperture.base,
			.size = workspace->aperture.size,
			.exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
		};
	workspace->overlays[2] =
		(struct payload_mm_authvar_mor_live_inventory_overlay) {
			.base = (uintptr_t)plan,
			.size = sizeof(*plan),
			.exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		};
	workspace->overlays[3] =
		(struct payload_mm_authvar_mor_live_inventory_overlay) {
			.base = (uintptr_t)binding,
			.size = sizeof(*binding),
			.exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		};
	workspace->overlays[4] =
		(struct payload_mm_authvar_mor_live_inventory_overlay) {
			.base = (uintptr_t)&clear_scratch,
			.size = sizeof(clear_scratch),
			.exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		};
	workspace->overlays[5] =
		(struct payload_mm_authvar_mor_live_inventory_overlay) {
			.base = workspace->transport.base,
			.size = workspace->transport.size,
			.exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		};
	if (starbook_mtl_mor_live_inventory_compose_with_overlays_owned(
		&workspace->guard, workspace->overlays,
		ARRAY_SIZE(workspace->overlays), &workspace->candidate,
		&workspace->inventory) != CB_SUCCESS ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_PREPARE) ||
	    memcmp(&workspace->reservations, reservations,
		sizeof(workspace->reservations)) ||
	    memcmp(&workspace->guard, dma_guard, sizeof(workspace->guard)) ||
	    memcmp(&workspace->transport, transport,
		sizeof(workspace->transport)) ||
	    payload_mm_authvar_mor_clear_x86_prepare(&workspace->candidate,
		(void *)(uintptr_t)workspace->page_tables.base,
		(void *)(uintptr_t)workspace->aperture.base, &binding->backend,
		&binding->ops) != CB_SUCCESS ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_PREPARE) ||
	    memcmp(&workspace->reservations, reservations,
		sizeof(workspace->reservations)) ||
	    memcmp(&workspace->guard, dma_guard, sizeof(workspace->guard)) ||
	    memcmp(&workspace->transport, transport,
		sizeof(workspace->transport)))
		goto out;
	if (!lifecycle_claim(binding, workspace->guard.generation)) {
		(void)poison_binding(NULL);
		goto out;
	}
	if (starbook_mtl_dma_guard_bind_owned(&workspace->candidate,
		&workspace->guard, &workspace->bound, &workspace->dma,
		&workspace->bind) != CB_SUCCESS ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_PREPARE) ||
	    memcmp(&workspace->reservations, reservations,
		sizeof(workspace->reservations)) ||
	    memcmp(&workspace->guard, dma_guard, sizeof(workspace->guard)) ||
	    memcmp(&workspace->transport, transport,
		sizeof(workspace->transport)) ||
	    !bytes_zero(plan, sizeof(*plan)))
		goto out;
	binding->ops.dma_snapshot = executor_dma_snapshot;
	binding->ops.inventory_context = &binding->backend;
	binding->ops.inventory_context_size = sizeof(binding->backend);
	binding->ops.inventory_validate = executor_inventory_validate;
	memset(&binding->authority, 0, sizeof(binding->authority));
	binding->authority.revision = STARBOOK_MTL_MOR_CLEAR_X86_REVISION;
	binding->authority.size = sizeof(binding->authority);
	binding->authority.owner = (uintptr_t)binding;
	binding->authority.plan_address = (uintptr_t)plan;
	binding->authority.page_tables = workspace->page_tables.base;
	binding->authority.aperture = workspace->aperture.base;
	binding->authority.transport = workspace->transport;
	binding->authority.prepared = workspace->guard;
	binding->authority.bound = workspace->bound;
	binding->authority.dma = workspace->dma;
	binding->authority.plan = workspace->candidate;
	binding->authority.backend = binding->backend;
	binding->authority.ops = binding->ops;
	memcpy(binding->authority.overlays, workspace->overlays,
		sizeof(workspace->overlays));
	binding->authority.seal = authority_seal(&binding->authority);
	binding->authority_mirror = binding->authority;
	__atomic_store_n(&binding->phase, MTL_MOR_CLEAR_BOUND, __ATOMIC_RELEASE);
	if (!authority_capture(binding, MTL_MOR_CLEAR_BOUND,
		&workspace->authority) ||
	    memcmp(&workspace->reservations, reservations,
		sizeof(workspace->reservations)) ||
	    memcmp(&workspace->guard, dma_guard, sizeof(workspace->guard)) ||
	    memcmp(&workspace->transport, transport,
		sizeof(workspace->transport)) ||
	    !bytes_zero(plan, sizeof(*plan)) ||
	    !scratch_owned(MTL_MOR_CLEAR_SCRATCH_PREPARE))
		goto out;
	*plan = workspace->candidate;
	status = CB_SUCCESS;
out:
	if (status == CB_SUCCESS &&
	    scratch_release(MTL_MOR_CLEAR_SCRATCH_PREPARE))
		return CB_SUCCESS;
	scratch_abort(MTL_MOR_CLEAR_SCRATCH_PREPARE);
	return fail(plan, binding);
}
