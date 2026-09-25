/* SPDX-License-Identifier: GPL-2.0-only */

#include "q35_mor_dma.h"

#include <boot/payload_mm_authvar_mor_clear_x86.h>
#include <boot/payload_mm_authvar_mor_linear.h>
#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <bootmem.h>
#include <commonlib/helpers.h>
#include <cpu/x86/pae.h>
#include <random.h>
#include <string.h>

#define Q35_MOR_LIMIT_EXCLUSIVE (1ULL << 32)

enum q35_mor_phase {
	Q35_MOR_EMPTY,
	Q35_MOR_CLASSIFYING,
	Q35_MOR_READY,
	Q35_MOR_RESERVING,
	Q35_MOR_RESERVED,
	Q35_MOR_RESOLVING,
	Q35_MOR_RESOLVED,
	Q35_MOR_COMPLETING,
	Q35_MOR_TERMINAL,
};

static const struct bootmem_aligned_reservation_request requests[] = {
	{
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(requests[0]),
		.bytes = PAE_PGTL_SIZE,
		.alignment = PAE_PGTL_ALIGN,
		.limit_exclusive = Q35_MOR_LIMIT_EXCLUSIVE,
		.tag = BM_MEM_TABLE,
	},
	{
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(requests[0]),
		.bytes = PAE_VMEM_SIZE,
		.alignment = PAE_VMEM_ALIGN,
		.limit_exclusive = Q35_MOR_LIMIT_EXCLUSIVE,
		.tag = BM_MEM_RESERVED,
	},
	{
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(requests[0]),
		.bytes = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
		.alignment = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_PAGE_SIZE,
		.limit_exclusive = Q35_MOR_LIMIT_EXCLUSIVE,
		.tag = BM_MEM_TABLE,
	},
};

static struct {
	struct bootmem_aligned_reservation_handle handles[ARRAY_SIZE(requests)];
	struct payload_mm_authvar_mor_live_inventory_request inventory;
	struct payload_mm_authvar_mor_clear_x86_backend backend;
	uint64_t generation;
	uint8_t capability[PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_CAPABILITY_SIZE];
	uint8_t receipt_secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];
	uint8_t phase;
	uint8_t request;
	uint8_t arena_seeded;
	uint8_t channel_seeded;
	uint8_t close_claimed;
} platform;

enum q35_mor_scratch_owner {
	Q35_MOR_SCRATCH_IDLE,
	Q35_MOR_SCRATCH_RESOLVE,
	Q35_MOR_SCRATCH_VALIDATE,
	Q35_MOR_SCRATCH_POISONED,
};

struct q35_mor_scratch {
	struct bootmem_aligned_reservation ranges[ARRAY_SIZE(requests)];
	struct payload_mm_authvar_mor_clear_dma_snapshot dma;
	struct payload_mm_authvar_mor_live_inventory_request inventory;
	struct payload_mm_authvar_mor_clear_plan validation_plan;
	struct payload_mm_authvar_mor_clear_plan original_plan;
	struct payload_mm_authvar_mor_clear_executor_ops original_executor;
	struct payload_mm_authvar_mor_live_inventory_workspace inventory_workspace;
	uint32_t owner;
};

static struct q35_mor_scratch scratch;

static __noinline void scrub(void *buffer, size_t size);

static bool random_bytes(uint8_t *bytes, size_t size)
{
	uint64_t value;
	bool success = true;

	for (size_t offset = 0; offset < size; offset += sizeof(value)) {
		if (get_random_number_64(&value) != CB_SUCCESS) {
			success = false;
			break;
		}
		memcpy(bytes + offset, &value, MIN(sizeof(value), size - offset));
	}
	scrub(&value, sizeof(value));
	return success;
}

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static void scrub_secrets(void)
{
	scrub(platform.capability, sizeof(platform.capability));
	scrub(platform.receipt_secret, sizeof(platform.receipt_secret));
}

static void platform_terminal_poison(void)
{
	__atomic_store_n(&platform.phase, Q35_MOR_TERMINAL, __ATOMIC_RELEASE);
	__atomic_store_n(&platform.arena_seeded, 3, __ATOMIC_RELEASE);
	__atomic_store_n(&platform.channel_seeded, 3, __ATOMIC_RELEASE);
	scrub_secrets();
}

static bool lifecycle_claim(uint8_t *state, uint8_t idle, uint8_t active,
	uint8_t terminal)
{
	uint8_t previous = __atomic_load_n(state, __ATOMIC_ACQUIRE);
	uint8_t next;

	do {
		next = previous == idle ? active : terminal;
	} while (!__atomic_compare_exchange_n(state, &previous, next, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
	return previous == idle;
}

#if ENV_TEST
void q35_mor_seed_claimed_test_hook(bool channel);
#endif

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t first_base = (uintptr_t)first;
	const uintptr_t second_base = (uintptr_t)second;

	if (!object_valid(first, first_size, 1) ||
	    !object_valid(second, second_size, 1))
		return true;
	if (first_base <= second_base)
		return second_base - first_base < first_size;
	return first_base - second_base < second_size;
}

static bool scratch_claim(uint32_t owner)
{
	uint32_t expected = Q35_MOR_SCRATCH_IDLE;

	if (!__atomic_compare_exchange_n(&scratch.owner, &expected, owner, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&scratch.owner, Q35_MOR_SCRATCH_POISONED,
			__ATOMIC_RELEASE);
		platform_terminal_poison();
		return false;
	}
	return true;
}

static bool scratch_release(uint32_t owner)
{
	uint32_t expected = owner;

	scrub(&scratch, offsetof(struct q35_mor_scratch, owner));
	if (!__atomic_compare_exchange_n(&scratch.owner, &expected,
		Q35_MOR_SCRATCH_IDLE, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&scratch.owner, Q35_MOR_SCRATCH_POISONED,
			__ATOMIC_RELEASE);
		platform_terminal_poison();
		return false;
	}
	return true;
}

static void scratch_abort(void)
{
	scrub(&scratch, offsetof(struct q35_mor_scratch, owner));
	__atomic_store_n(&scratch.owner, Q35_MOR_SCRATCH_POISONED,
		__ATOMIC_RELEASE);
}

static bool plan_excludes(const struct payload_mm_authvar_mor_clear_plan *plan,
	const void *object, size_t size)
{
	const uintptr_t base = (uintptr_t)object;

	for (size_t index = 0; index < plan->span_count; index++) {
		const struct payload_mm_authvar_mor_grant_span *span =
			&plan->spans[index];

		if (span->span_class == PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED &&
		    base >= span->base && size <= span->size &&
		    base - span->base <= span->size - size)
			return true;
	}
	return false;
}

static enum cb_err classify_guard(void *context,
	struct payload_mm_authvar_mor_linear_boot *boot)
{
	struct payload_mm_authvar_mor_linear_boot original;
	uint8_t expected;

	if (context != &platform ||
	    !object_valid(boot, sizeof(*boot), _Alignof(*boot)) ||
	    ranges_overlap(boot, sizeof(*boot), &platform, sizeof(platform)) ||
	    ranges_overlap(boot, sizeof(*boot), &scratch, sizeof(scratch)))
		return CB_ERR;
	memcpy(&original, boot, sizeof(original));
	if (!lifecycle_claim(&platform.phase, Q35_MOR_EMPTY,
		Q35_MOR_CLASSIFYING, Q35_MOR_TERMINAL) ||
	    !q35_mor_dma_pre_device_guard_valid() ||
	    get_random_number_64(&platform.generation) != CB_SUCCESS ||
	    !platform.generation ||
	    !random_bytes(platform.capability, sizeof(platform.capability)) ||
	    !random_bytes(platform.receipt_secret, sizeof(platform.receipt_secret)))
		goto fail;
	*boot = (struct payload_mm_authvar_mor_linear_boot) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION,
		.size = sizeof(*boot),
		.generation = platform.generation,
		.kind = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT,
	};
	expected = Q35_MOR_CLASSIFYING;
	if (!__atomic_compare_exchange_n(&platform.phase, &expected,
		Q35_MOR_READY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		memcpy(boot, &original, sizeof(original));
		goto fail;
	}
	scrub(&original, sizeof(original));
	return CB_SUCCESS;
fail:
	memcpy(boot, &original, sizeof(original));
	scrub(&original, sizeof(original));
	scrub_secrets();
	__atomic_exchange_n(&platform.phase, Q35_MOR_TERMINAL, __ATOMIC_ACQ_REL);
	return CB_ERR;
}

static enum cb_err reservations_register(void *context)
{
	uint8_t expected;

	if (context != &platform)
		return CB_ERR;
	if (!lifecycle_claim(&platform.phase, Q35_MOR_READY,
		Q35_MOR_RESERVING, Q35_MOR_TERMINAL) ||
	    bootmem_aligned_reservations_register(requests, ARRAY_SIZE(requests),
		platform.handles)) {
		__atomic_exchange_n(&platform.phase, Q35_MOR_TERMINAL,
			__ATOMIC_ACQ_REL);
		return CB_ERR;
	}
	expected = Q35_MOR_RESERVING;
	if (!__atomic_compare_exchange_n(&platform.phase, &expected,
		Q35_MOR_RESERVED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	__atomic_store_n(&platform.request, 1, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

static enum cb_err dma_snapshot(void *context,
	struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot)
{
	return context == &platform.backend ? q35_mor_dma_snapshot(snapshot) : CB_ERR;
}

static enum cb_err inventory_validate(void *context,
	const struct payload_mm_authvar_mor_clear_plan *plan)
{
	enum cb_err status = CB_ERR;

	if (context != &platform || !scratch_claim(Q35_MOR_SCRATCH_VALIDATE))
		return CB_ERR;
	if (payload_mm_authvar_mor_live_inventory_compose_owned(
		&platform.inventory, &scratch.validation_plan,
		&scratch.inventory_workspace) == CB_SUCCESS &&
	    !memcmp(&scratch.validation_plan, plan, sizeof(*plan)))
		status = CB_SUCCESS;
	if (status != CB_SUCCESS) {
		scratch_abort();
		return CB_ERR;
	}
	if (!scratch_release(Q35_MOR_SCRATCH_VALIDATE))
		return CB_ERR;
	return status;
}

static enum cb_err resolve_binding(void *context, uint64_t generation,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct payload_mm_authvar_mor_clear_executor_ops *executor)
{
	uint8_t expected;
	bool outputs_owned = false;

	if (context != &platform || generation != platform.generation ||
	    !object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !object_valid(executor, sizeof(*executor), _Alignof(*executor)) ||
	    ranges_overlap(plan, sizeof(*plan), executor, sizeof(*executor)) ||
	    ranges_overlap(plan, sizeof(*plan), &platform, sizeof(platform)) ||
	    ranges_overlap(executor, sizeof(*executor), &platform, sizeof(platform)) ||
	    ranges_overlap(plan, sizeof(*plan), &scratch, sizeof(scratch)) ||
	    ranges_overlap(executor, sizeof(*executor), &scratch, sizeof(scratch)) ||
	    !scratch_claim(Q35_MOR_SCRATCH_RESOLVE))
		return CB_ERR;
	memcpy(&scratch.original_plan, plan, sizeof(*plan));
	memcpy(&scratch.original_executor, executor, sizeof(*executor));
	if (!lifecycle_claim(&platform.phase, Q35_MOR_RESERVED,
		Q35_MOR_RESOLVING, Q35_MOR_TERMINAL))
		goto fail;
	for (size_t index = 0; index < ARRAY_SIZE(scratch.ranges); index++) {
		if (bootmem_aligned_reservation_query(&platform.handles[index],
			&scratch.ranges[index]) ||
		    scratch.ranges[index].size != requests[index].bytes ||
		    scratch.ranges[index].tag != requests[index].tag ||
		    scratch.ranges[index].base % requests[index].alignment ||
		    scratch.ranges[index].base >= requests[index].limit_exclusive ||
		    scratch.ranges[index].size > requests[index].limit_exclusive -
			 scratch.ranges[index].base ||
		    ranges_overlap((void *)(uintptr_t)scratch.ranges[index].base,
			scratch.ranges[index].size, plan, sizeof(*plan)) ||
		    ranges_overlap((void *)(uintptr_t)scratch.ranges[index].base,
			scratch.ranges[index].size, executor, sizeof(*executor)) ||
		    ranges_overlap((void *)(uintptr_t)scratch.ranges[index].base,
			scratch.ranges[index].size, &platform, sizeof(platform)) ||
		    ranges_overlap((void *)(uintptr_t)scratch.ranges[index].base,
			scratch.ranges[index].size, &scratch, sizeof(scratch)))
			goto fail;
		for (size_t prior = 0; prior < index; prior++)
			if (scratch.ranges[index].base < scratch.ranges[prior].base +
			    scratch.ranges[prior].size &&
			    scratch.ranges[prior].base < scratch.ranges[index].base +
			    scratch.ranges[index].size)
				goto fail;
	}
	if (q35_mor_dma_snapshot(&scratch.dma) != CB_SUCCESS)
		goto fail;
	scratch.inventory = (struct payload_mm_authvar_mor_live_inventory_request) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_REVISION,
		.size = sizeof(scratch.inventory),
		.generation = scratch.dma.generation,
		.overlay_count = ARRAY_SIZE(scratch.ranges),
	};
	memcpy(scratch.inventory.identity, scratch.dma.identity,
		sizeof(scratch.dma.identity));
	for (size_t index = 0; index < ARRAY_SIZE(scratch.ranges); index++) {
		scratch.inventory.overlays[index].base = scratch.ranges[index].base;
		scratch.inventory.overlays[index].size = scratch.ranges[index].size;
		scratch.inventory.overlays[index].exclusion_reason = index == 1 ?
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED :
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE;
	}
	memset(plan, 0, sizeof(*plan));
	memset(executor, 0, sizeof(*executor));
	outputs_owned = true;
	if (payload_mm_authvar_mor_live_inventory_compose_owned(&scratch.inventory,
		plan, &scratch.inventory_workspace) != CB_SUCCESS ||
	    !plan_excludes(plan, &scratch, sizeof(scratch)) ||
	    payload_mm_authvar_mor_clear_x86_prepare(plan,
		(void *)(uintptr_t)scratch.ranges[0].base,
		(void *)(uintptr_t)scratch.ranges[1].base,
		&platform.backend, executor) != CB_SUCCESS)
		goto fail;
	platform.inventory = scratch.inventory;
	executor->context = &platform.backend;
	executor->dma_snapshot = dma_snapshot;
	executor->inventory_context = &platform;
	executor->inventory_context_size = sizeof(platform);
	executor->inventory_validate = inventory_validate;
	expected = Q35_MOR_RESOLVING;
	if (!__atomic_compare_exchange_n(&platform.phase, &expected,
		Q35_MOR_RESOLVED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	if (!scratch_release(Q35_MOR_SCRATCH_RESOLVE))
		goto fail_without_scratch;
	return CB_SUCCESS;
fail:
	if (outputs_owned) {
		memcpy(plan, &scratch.original_plan, sizeof(*plan));
		memcpy(executor, &scratch.original_executor, sizeof(*executor));
	}
	scrub(&platform.inventory, sizeof(platform.inventory));
	scrub(&platform.backend, sizeof(platform.backend));
	scratch_abort();
fail_without_scratch:
	__atomic_exchange_n(&platform.phase, Q35_MOR_TERMINAL, __ATOMIC_ACQ_REL);
	return CB_ERR;
}

static enum cb_err private_complete(void *context,
	const struct payload_mm_authvar_mor_grant *grant)
{
	uint8_t expected = Q35_MOR_RESOLVED;

	if (context != &platform ||
	    !object_valid(grant, sizeof(*grant), _Alignof(*grant)) ||
	    ranges_overlap(grant, sizeof(*grant), &platform, sizeof(platform)) ||
	    ranges_overlap(grant, sizeof(*grant), &scratch, sizeof(scratch)) ||
	    !__atomic_compare_exchange_n(&platform.phase, &expected,
		Q35_MOR_COMPLETING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    __atomic_exchange_n(&platform.close_claimed, 1, __ATOMIC_ACQ_REL))
		return CB_ERR;
	if (payload_mm_authvar_mor_private_smi_send_install(grant) != CB_SUCCESS) {
		__atomic_store_n(&platform.phase, Q35_MOR_TERMINAL, __ATOMIC_RELEASE);
		return CB_ERR;
	}
	__atomic_store_n(&platform.phase, Q35_MOR_TERMINAL, __ATOMIC_RELEASE);
	return CB_SUCCESS;
}

static enum cb_err private_close(void *context)
{
	if (context != &platform ||
	    __atomic_exchange_n(&platform.close_claimed, 1, __ATOMIC_ACQ_REL))
		return CB_ERR;
	__atomic_store_n(&platform.phase, Q35_MOR_TERMINAL, __ATOMIC_RELEASE);
	scrub_secrets();
	return __atomic_load_n(&platform.request, __ATOMIC_ACQUIRE) ?
		payload_mm_authvar_mor_private_smi_close_unused() : CB_SUCCESS;
}

bool platform_payload_mm_authvar_mor_linear_ops(
	struct payload_mm_authvar_mor_linear_ops *ops)
{
	if (!object_valid(ops, sizeof(*ops), _Alignof(*ops)) ||
	    ranges_overlap(ops, sizeof(*ops), &platform, sizeof(platform)) ||
	    ranges_overlap(ops, sizeof(*ops), &scratch, sizeof(scratch)))
		return false;
	*ops = (struct payload_mm_authvar_mor_linear_ops) {
		.context = &platform,
		.context_size = sizeof(platform),
		.classify_guard = classify_guard,
		.reservations_register = reservations_register,
		.resolve_binding = resolve_binding,
		.private_complete = private_complete,
		.private_close = private_close,
	};
	return true;
}

bool platform_payload_mm_authvar_smm_arena_seed(
	struct payload_mm_authvar_smm_arena_seed *seed)
{
	struct payload_mm_authvar_smm_arena_seed candidate = { 0 };
	struct payload_mm_authvar_smm_arena_seed original;
	uint8_t expected = 0;
	bool published = false;

	if (!object_valid(seed, sizeof(*seed), _Alignof(*seed)) ||
	    ranges_overlap(seed, sizeof(*seed), &platform, sizeof(platform)) ||
	    ranges_overlap(seed, sizeof(*seed), &scratch, sizeof(scratch))) {
		platform_terminal_poison();
		return false;
	}
	memcpy(&original, seed, sizeof(original));
	if (!__atomic_load_n(&platform.request, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&platform.phase, __ATOMIC_ACQUIRE) != Q35_MOR_RESERVED ||
	    !lifecycle_claim(&platform.arena_seeded, 0, 1, 3) ||
	    !platform.generation) {
		scrub_secrets();
		goto fail;
	}
#if ENV_TEST
	q35_mor_seed_claimed_test_hook(false);
#endif
	candidate = (struct payload_mm_authvar_smm_arena_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION,
		.size = sizeof(candidate),
		.cold_boot_generation = platform.generation,
	};
	memcpy(candidate.owner, platform.capability, sizeof(candidate.owner));
	*seed = candidate;
	published = true;
	expected = 1;
	if (!__atomic_compare_exchange_n(&platform.arena_seeded, &expected, 2,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	return true;
fail:
	if (published)
		memcpy(seed, &original, sizeof(original));
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	platform_terminal_poison();
	return false;
}

bool platform_payload_mm_authvar_smm_arena_required(void)
{
	return __atomic_load_n(&platform.request, __ATOMIC_ACQUIRE) == 1;
}

void platform_payload_mm_authvar_smm_arena_abort(void)
{
	platform_terminal_poison();
}

bool platform_payload_mm_authvar_mor_private_smi_required(void)
{
	return __atomic_load_n(&platform.request, __ATOMIC_ACQUIRE) == 1;
}

bool platform_payload_mm_authvar_mor_private_smi_seed(
	struct payload_mm_authvar_mor_private_smi_seed *seed)
{
	struct payload_mm_authvar_mor_private_smi_seed candidate = { 0 };
	struct payload_mm_authvar_mor_private_smi_seed original;
	uint8_t expected = 0;
	bool published = false;

	if (!object_valid(seed, sizeof(*seed), _Alignof(*seed)) ||
	    ranges_overlap(seed, sizeof(*seed), &platform, sizeof(platform)) ||
	    ranges_overlap(seed, sizeof(*seed), &scratch, sizeof(scratch))) {
		platform_terminal_poison();
		return false;
	}
	memcpy(&original, seed, sizeof(original));
	if (!__atomic_load_n(&platform.request, __ATOMIC_ACQUIRE) ||
	    __atomic_load_n(&platform.phase, __ATOMIC_ACQUIRE) != Q35_MOR_RESERVED ||
	    __atomic_load_n(&platform.arena_seeded, __ATOMIC_ACQUIRE) != 2 ||
	    !lifecycle_claim(&platform.channel_seeded, 0, 1, 3)) {
		scrub_secrets();
		goto fail;
	}
#if ENV_TEST
	q35_mor_seed_claimed_test_hook(true);
#endif
	candidate = (struct payload_mm_authvar_mor_private_smi_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI_REVISION,
		.size = sizeof(candidate),
		.cold_boot_generation = platform.generation,
		.page_handle = platform.handles[2],
	};
	memcpy(candidate.capability, platform.capability,
		sizeof(candidate.capability));
	memcpy(candidate.receipt_secret, platform.receipt_secret,
		sizeof(candidate.receipt_secret));
	*seed = candidate;
	published = true;
	expected = 1;
	if (!__atomic_compare_exchange_n(&platform.channel_seeded, &expected, 2,
		false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	scrub_secrets();
	return true;
fail:
	if (published)
		memcpy(seed, &original, sizeof(original));
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	platform_terminal_poison();
	return false;
}
