/* SPDX-License-Identifier: GPL-2.0-only */

#include "mor_platform.h"

#include "dma_guard.h"
#include "mor_clear_x86.h"
#include "mor_cold_boot.h"
#include "mor_early_dma.h"

#include <boot/payload_mm_authvar_mor_linear.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <commonlib/helpers.h>
#include <random.h>
#include <limits.h>
#include <string.h>

_Static_assert(STARBOOK_MTL_MOR_PRIVATE_TRANSPORT_SIZE ==
	STARBOOK_MTL_MOR_CLEAR_X86_TRANSPORT_SIZE,
	"private boundary and clear transport sizes differ");

struct mtl_mor_platform {
	struct starbook_mtl_mor_private_boundary_ops private;
	struct starbook_mtl_mor_clear_x86_reservations reservations;
	struct starbook_mtl_mor_clear_x86_binding binding;
	struct starbook_mtl_dma_guard_snapshot guard;
	struct bootmem_aligned_reservation transport;
	uint8_t owner[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE];
	uint64_t generation;
	uint32_t boot_kind;
	uint32_t seed_state;
	uint32_t callback_state;
	uint32_t classified;
	uint32_t reserved;
	uint32_t resolved;
};

static struct mtl_mor_platform platform;

struct mtl_mor_platform_scratch {
	struct mtl_mor_platform frozen;
	uint32_t owner;
};

struct mtl_mor_private_cleanup {
	void *context;
	enum cb_err (*close)(void *context);
};

enum mtl_mor_cleanup_state {
	MTL_MOR_CLEANUP_EMPTY,
	MTL_MOR_CLEANUP_ARMING,
	MTL_MOR_CLEANUP_OPEN,
	MTL_MOR_CLEANUP_CONSUMED,
};

static struct {
	struct mtl_mor_private_cleanup authority;
	struct mtl_mor_private_cleanup mirror;
	uint32_t state;
} cleanup;

/* This provider is ramstage-only; keep its large immutable view off the stack. */
static struct mtl_mor_platform_scratch scratch;

enum mtl_mor_scratch_owner {
	MTL_MOR_SCRATCH_IDLE,
	MTL_MOR_SCRATCH_SEED,
	MTL_MOR_SCRATCH_RESERVATIONS,
	MTL_MOR_SCRATCH_RESOLVE,
	MTL_MOR_SCRATCH_COMPLETE,
	MTL_MOR_SCRATCH_CLOSE,
	MTL_MOR_SCRATCH_POISONED,
};

enum mtl_mor_seed_state {
	MTL_MOR_SEED_EMPTY,
	MTL_MOR_SEEDING,
	MTL_MOR_SEEDED,
	MTL_MOR_SEED_POISONED,
};

enum mtl_mor_callback_state {
	MTL_MOR_CALLBACK_IDLE,
	MTL_MOR_CALLBACK_ACTIVE,
	MTL_MOR_CALLBACK_POISONED,
};

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool ranges_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	const uintptr_t left_base = (uintptr_t)left;
	const uintptr_t right_base = (uintptr_t)right;

	if (!object_valid(left, left_size, 1) ||
	    !object_valid(right, right_size, 1))
		return true;
	if (left_base <= right_base)
		return right_base - left_base < left_size;
	return left_base - right_base < right_size;
}

static bool provider_object_valid(const void *object, size_t size,
	size_t alignment)
{
	return object_valid(object, size, alignment) &&
		!ranges_overlap(object, size, &platform, sizeof(platform)) &&
		!ranges_overlap(object, size, &scratch, sizeof(scratch)) &&
		!ranges_overlap(object, size, &cleanup, sizeof(cleanup));
}

__weak bool starbook_mtl_mor_private_boundary(
	struct starbook_mtl_mor_private_boundary_ops *ops)
{
	if (ops)
		memset(ops, 0, sizeof(*ops));
	return false;
}

#if ENV_TEST
__weak void starbook_mtl_mor_platform_resolve_return_test_hook(
	struct starbook_mtl_mor_private_boundary_ops *ops)
{
	(void)ops;
}

__weak void starbook_mtl_mor_platform_resolve_preflight_test_hook(
	struct starbook_mtl_mor_private_boundary_ops *ops)
{
	(void)ops;
}

__weak void starbook_mtl_mor_platform_resolve_release_test_hook(void)
{
}
#endif

static bool bytes_nonzero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	for (size_t index = 0; index < size; index++)
		combined |= bytes[index];
	return combined;
}

static bool transport_valid(
	const struct bootmem_aligned_reservation *transport)
{
	return transport->size == STARBOOK_MTL_MOR_PRIVATE_TRANSPORT_SIZE &&
		transport->tag == BM_MEM_TABLE && !transport->reserved &&
		!(transport->base % STARBOOK_MTL_MOR_PRIVATE_TRANSPORT_SIZE) &&
		transport->base <= UINTPTR_MAX - (transport->size - 1U);
}

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool cleanup_arm(const struct mtl_mor_private_cleanup *authority)
{
	uint32_t expected = MTL_MOR_CLEANUP_EMPTY;

	if (!authority->context || !authority->close ||
	    !__atomic_compare_exchange_n(&cleanup.state, &expected,
		MTL_MOR_CLEANUP_ARMING, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return false;
	cleanup.authority = *authority;
	cleanup.mirror = *authority;
	__atomic_store_n(&cleanup.state, MTL_MOR_CLEANUP_OPEN, __ATOMIC_RELEASE);
	return true;
}

static enum cb_err cleanup_take(bool invoke)
{
	struct mtl_mor_private_cleanup authority;
	enum cb_err status = CB_SUCCESS;
	uint32_t expected = MTL_MOR_CLEANUP_OPEN;

	if (!__atomic_compare_exchange_n(&cleanup.state, &expected,
		MTL_MOR_CLEANUP_CONSUMED, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return expected == MTL_MOR_CLEANUP_CONSUMED ? CB_SUCCESS : CB_ERR;
	authority = cleanup.authority;
	if (!authority.context || !authority.close ||
	    memcmp(&cleanup.authority, &cleanup.mirror,
		sizeof(cleanup.authority))) {
		scrub(&cleanup.authority, sizeof(cleanup.authority));
		scrub(&cleanup.mirror, sizeof(cleanup.mirror));
		return CB_ERR;
	}
	scrub(&cleanup.authority, sizeof(cleanup.authority));
	scrub(&cleanup.mirror, sizeof(cleanup.mirror));
	if (invoke)
		status = authority.close(authority.context);
	scrub(&authority, sizeof(authority));
	return status;
}

static void cleanup_failure(void)
{
	(void)cleanup_take(true);
}

static void scratch_terminal_poison(void)
{
	const uint32_t previous = __atomic_exchange_n(&scratch.owner,
		MTL_MOR_SCRATCH_POISONED, __ATOMIC_ACQ_REL);

	/* An active owner alone may touch its snapshot after being poisoned. */
	if (previous == MTL_MOR_SCRATCH_IDLE)
		scrub(&scratch.frozen, sizeof(scratch.frozen));
}

static struct mtl_mor_platform *scratch_claim(uint32_t owner)
{
	uint32_t expected = MTL_MOR_SCRATCH_IDLE;

	if (!owner || owner >= MTL_MOR_SCRATCH_POISONED ||
	    !__atomic_compare_exchange_n(&scratch.owner, &expected, owner, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&scratch.owner, MTL_MOR_SCRATCH_POISONED,
			__ATOMIC_RELEASE);
		__atomic_store_n(&platform.seed_state, MTL_MOR_SEED_POISONED,
			__ATOMIC_RELEASE);
		return NULL;
	}
	return &scratch.frozen;
}

static bool scratch_release(uint32_t owner)
{
	uint32_t expected = owner;

	scrub(&scratch.frozen, sizeof(scratch.frozen));
	if (__atomic_compare_exchange_n(&scratch.owner, &expected,
		MTL_MOR_SCRATCH_IDLE, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return true;
	scratch_terminal_poison();
	return false;
}

static void scratch_abort(uint32_t owner)
{
	uint32_t current = __atomic_load_n(&scratch.owner, __ATOMIC_ACQUIRE);

	/* Only a successful claimant calls this and therefore owns frozen. */
	if (current == owner || current == MTL_MOR_SCRATCH_POISONED)
		scrub(&scratch.frozen, sizeof(scratch.frozen));
	__atomic_store_n(&scratch.owner, MTL_MOR_SCRATCH_POISONED,
		__ATOMIC_RELEASE);
}

static bool private_valid(
	const struct starbook_mtl_mor_private_boundary_ops *ops)
{
	return ops->revision == STARBOOK_MTL_MOR_PRIVATE_BOUNDARY_REVISION &&
		ops->size == sizeof(*ops) && ops->context && ops->context_size &&
		ops->callback_stack_bytes &&
		ops->callback_stack_bytes <=
			STARBOOK_MTL_MOR_PRIVATE_CALLBACK_STACK_MAX &&
		!ops->reserved &&
		provider_object_valid(ops->context, ops->context_size, 1) &&
		!ranges_overlap(ops->context, ops->context_size, ops,
			sizeof(*ops)) &&
		ops->arena_seed && ops->reservations_register && ops->resolve &&
		ops->complete && ops->close;
}

static bool private_context_disjoint(const void *object, size_t size)
{
	return provider_object_valid(object, size, 1) &&
		!ranges_overlap(platform.private.context,
			platform.private.context_size, object, size);
}

static void platform_snapshot(struct mtl_mor_platform *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));
	memcpy(&snapshot->private, &platform.private, sizeof(snapshot->private));
	memcpy(&snapshot->reservations, &platform.reservations,
		sizeof(snapshot->reservations));
	memcpy(&snapshot->binding, &platform.binding, sizeof(snapshot->binding));
	memcpy(&snapshot->guard, &platform.guard, sizeof(snapshot->guard));
	memcpy(&snapshot->transport, &platform.transport,
		sizeof(snapshot->transport));
	memcpy(snapshot->owner, platform.owner, sizeof(snapshot->owner));
	snapshot->generation = platform.generation;
	snapshot->boot_kind = platform.boot_kind;
	snapshot->seed_state = __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE);
	snapshot->callback_state = __atomic_load_n(&platform.callback_state,
		__ATOMIC_ACQUIRE);
	snapshot->classified = platform.classified;
	snapshot->reserved = platform.reserved;
	snapshot->resolved = platform.resolved;
}

static bool platform_matches(const struct mtl_mor_platform *snapshot)
{
	return !memcmp(&platform.private, &snapshot->private,
			sizeof(platform.private)) &&
		!memcmp(&platform.reservations, &snapshot->reservations,
			sizeof(platform.reservations)) &&
		!memcmp(&platform.binding, &snapshot->binding,
			sizeof(platform.binding)) &&
		!memcmp(&platform.guard, &snapshot->guard,
			sizeof(platform.guard)) &&
		!memcmp(&platform.transport, &snapshot->transport,
			sizeof(platform.transport)) &&
		!memcmp(platform.owner, snapshot->owner, sizeof(platform.owner)) &&
		platform.generation == snapshot->generation &&
		platform.boot_kind == snapshot->boot_kind &&
		__atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE) ==
			snapshot->seed_state &&
		__atomic_load_n(&platform.callback_state, __ATOMIC_ACQUIRE) ==
			snapshot->callback_state &&
		platform.classified == snapshot->classified &&
		platform.reserved == snapshot->reserved &&
		platform.resolved == snapshot->resolved;
}

static bool private_callback_enter(void)
{
	uint32_t expected = MTL_MOR_CALLBACK_IDLE;

	if (__atomic_compare_exchange_n(&platform.callback_state, &expected,
		MTL_MOR_CALLBACK_ACTIVE, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return true;
	__atomic_store_n(&platform.seed_state, MTL_MOR_SEED_POISONED,
		__ATOMIC_RELEASE);
#if ENV_TEST
	starbook_mtl_mor_platform_claim_conflict_test_hook();
#endif
	return false;
}

static bool private_callback_leave(bool valid)
{
	uint32_t expected = MTL_MOR_CALLBACK_ACTIVE;

#if ENV_TEST
	if (valid)
		starbook_mtl_mor_platform_leave_test_hook();
#endif
	valid = valid && __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE) !=
		MTL_MOR_SEED_POISONED;
	if (valid && __atomic_compare_exchange_n(&platform.callback_state,
		&expected, MTL_MOR_CALLBACK_IDLE, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return true;
	__atomic_store_n(&platform.callback_state, MTL_MOR_CALLBACK_POISONED,
		__ATOMIC_RELEASE);
	return false;
}

static void provider_terminal_poison(void)
{
	__atomic_store_n(&platform.seed_state, MTL_MOR_SEED_POISONED,
		__ATOMIC_RELEASE);
	scrub(platform.owner, sizeof(platform.owner));
	scratch_terminal_poison();
	__atomic_store_n(&platform.callback_state, MTL_MOR_CALLBACK_POISONED,
		__ATOMIC_RELEASE);
}

static bool private_callback_terminal_leave(void)
{
	uint32_t expected = MTL_MOR_CALLBACK_ACTIVE;

#if ENV_TEST
	starbook_mtl_mor_platform_leave_test_hook();
#endif
	if (__atomic_load_n(&cleanup.state, __ATOMIC_ACQUIRE) !=
			MTL_MOR_CLEANUP_CONSUMED ||
	    __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE) ==
			MTL_MOR_SEED_POISONED ||
	    !__atomic_compare_exchange_n(&platform.callback_state, &expected,
		MTL_MOR_CALLBACK_POISONED, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE))
		return false;
	__atomic_store_n(&platform.seed_state, MTL_MOR_SEED_POISONED,
		__ATOMIC_RELEASE);
	scrub(platform.owner, sizeof(platform.owner));
	scratch_terminal_poison();
	return true;
}

static bool seeded_and_disjoint(const void *object, size_t size)
{
	return __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE) ==
		MTL_MOR_SEEDED && bytes_nonzero(platform.owner, sizeof(platform.owner)) &&
		(!object || private_context_disjoint(object, size));
}

static enum cb_err load_private(void)
{
	struct starbook_mtl_mor_private_boundary_ops candidate = { 0 };

	if (private_valid(&platform.private))
		return CB_SUCCESS;
	if (!starbook_mtl_mor_private_boundary(&candidate) ||
	    !private_valid(&candidate))
		return CB_ERR;
	platform.private = candidate;
	return CB_SUCCESS;
}

static enum cb_err classify_retained(void)
{
	uint32_t kind = STARBOOK_MTL_MOR_BOOT_UNKNOWN;
	uint64_t generation = 0;
	struct starbook_mtl_dma_guard_snapshot guard = { 0 };

	if (!private_context_disjoint(&kind, sizeof(kind)) ||
	    !private_context_disjoint(&generation, sizeof(generation)) ||
	    !private_context_disjoint(&guard, sizeof(guard)))
		return CB_ERR;
	if (starbook_mtl_mor_early_dma_classify(&kind, &generation, &guard) !=
	    CB_SUCCESS || !generation ||
	    (kind != STARBOOK_MTL_MOR_BOOT_COLD &&
	     kind != STARBOOK_MTL_MOR_BOOT_S3))
		return CB_ERR;
	if (platform.classified) {
		if (platform.boot_kind != kind || platform.generation != generation ||
		    (kind == STARBOOK_MTL_MOR_BOOT_COLD &&
		     memcmp(&platform.guard, &guard, sizeof(guard))))
			return CB_ERR;
		return CB_SUCCESS;
	}
	platform.boot_kind = kind;
	platform.generation = generation;
	platform.guard = guard;
	platform.classified = 1U;
	return CB_SUCCESS;
}

static void close_private_seed(void)
{
	__atomic_store_n(&platform.seed_state, MTL_MOR_SEED_POISONED,
		__ATOMIC_RELEASE);
	cleanup_failure();
}

static enum cb_err ensure_seed(const void *live_object, size_t live_size,
	const void *candidate, size_t candidate_size,
	const void *original, size_t original_size)
{
	uint64_t entropy[STARBOOK_MTL_MOR_PRIVATE_OWNER_SIZE / sizeof(uint64_t)];
	struct mtl_mor_platform *frozen = NULL;
	uint32_t state;
	uint32_t expected;
	bool private_seed_live = false;

	if (load_private() != CB_SUCCESS ||
	    !private_context_disjoint(live_object, live_size) ||
	    !private_context_disjoint(candidate, candidate_size) ||
	    !private_context_disjoint(original, original_size) ||
	    !private_context_disjoint(entropy, sizeof(entropy)))
		goto fail;
	state = __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE);
	expected = state;
	if ((state != MTL_MOR_SEED_EMPTY && state != MTL_MOR_SEEDED) ||
	    !__atomic_compare_exchange_n(&platform.seed_state, &expected,
		MTL_MOR_SEEDING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&platform.seed_state, MTL_MOR_SEED_POISONED,
			__ATOMIC_RELEASE);
		return CB_ERR;
	}
	if (state == MTL_MOR_SEEDED) {
		private_seed_live = true;
		if (!bytes_nonzero(platform.owner, sizeof(platform.owner)))
			goto fail;
		expected = MTL_MOR_SEEDING;
		if (!__atomic_compare_exchange_n(&platform.seed_state, &expected,
			MTL_MOR_SEEDED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
			goto fail;
		return CB_SUCCESS;
	}
	if (classify_retained() != CB_SUCCESS)
		goto fail;
	frozen = scratch_claim(MTL_MOR_SCRATCH_SEED);
	if (!frozen)
		goto fail;
	memset(entropy, 0, sizeof(entropy));
	for (size_t index = 0; index < ARRAY_SIZE(entropy); index++)
		if (get_random_number_64(&entropy[index]) != CB_SUCCESS)
			goto fail;
	memcpy(platform.owner, entropy, sizeof(platform.owner));
	memset(entropy, 0, sizeof(entropy));
	if (!bytes_nonzero(platform.owner, sizeof(platform.owner)))
		goto fail;
	platform_snapshot(frozen);
	if (!cleanup_arm(&(const struct mtl_mor_private_cleanup) {
		.context = frozen->private.context,
		.close = frozen->private.close,
	}))
		goto fail;
	private_seed_live = true;
	if (platform.private.arena_seed(platform.private.context,
		platform.generation, platform.owner) != CB_SUCCESS)
		goto fail;
	if (!platform_matches(frozen))
		goto fail;
	if (!scratch_release(MTL_MOR_SCRATCH_SEED)) {
		frozen = NULL;
		goto fail;
	}
	frozen = NULL;
	expected = MTL_MOR_SEEDING;
	if (!__atomic_compare_exchange_n(&platform.seed_state, &expected,
		MTL_MOR_SEEDED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		goto fail;
	return CB_SUCCESS;
fail:
	__atomic_store_n(&platform.seed_state, MTL_MOR_SEED_POISONED,
		__ATOMIC_RELEASE);
	memset(entropy, 0, sizeof(entropy));
	scrub(platform.owner, sizeof(platform.owner));
	if (frozen)
		scratch_abort(MTL_MOR_SCRATCH_SEED);
	if (private_seed_live)
		close_private_seed();
	return CB_ERR;
}

bool platform_payload_mm_authvar_smm_arena_required(void)
{
	return __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE) ==
			MTL_MOR_SEEDED &&
		__atomic_load_n(&platform.callback_state, __ATOMIC_ACQUIRE) ==
			MTL_MOR_CALLBACK_IDLE &&
		platform.classified == 1U &&
		platform.boot_kind == STARBOOK_MTL_MOR_BOOT_COLD &&
		platform.generation && platform.reserved == 1U &&
		bytes_nonzero(platform.owner, sizeof(platform.owner));
}

bool platform_payload_mm_authvar_smm_arena_seed(
	struct payload_mm_authvar_smm_arena_seed *seed)
{
	struct payload_mm_authvar_smm_arena_seed candidate;
	uint8_t original[sizeof(*seed)];
	bool private_seed_live = false;

	if (!provider_object_valid(seed, sizeof(*seed), _Alignof(*seed)))
		return false;
	memcpy(original, seed, sizeof(original));
#if ENV_TEST
	starbook_mtl_mor_platform_constructor_scratch_test_hook(&candidate,
		sizeof(candidate), &original, sizeof(original));
#endif
	if (!private_callback_enter())
		return false;
	if (ensure_seed(seed, sizeof(*seed), &candidate, sizeof(candidate),
		&original, sizeof(original)) != CB_SUCCESS)
		goto fail;
	private_seed_live = true;
	if (!seeded_and_disjoint(seed, sizeof(*seed)))
		goto fail;
	candidate = (struct payload_mm_authvar_smm_arena_seed) {
		.revision = PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION,
		.size = sizeof(candidate),
		.cold_boot_generation = platform.generation,
	};
	memcpy(candidate.owner, platform.owner, sizeof(candidate.owner));
	*seed = candidate;
	if (!private_callback_leave(true)) {
		memcpy(seed, original, sizeof(original));
		goto fail_without_leave;
	}
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	return true;
fail:
	(void)private_callback_leave(false);
fail_without_leave:
	if (private_seed_live)
		close_private_seed();
	provider_terminal_poison();
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	return false;
}

static enum cb_err classify_guard(void *context,
	struct payload_mm_authvar_mor_linear_boot *boot)
{
	struct mtl_mor_platform *state = context;
	struct payload_mm_authvar_mor_linear_boot candidate;
	uint8_t original[sizeof(*boot)];

	if (state != &platform ||
	    !provider_object_valid(boot, sizeof(*boot), _Alignof(*boot)))
		return CB_ERR;
	memcpy(original, boot, sizeof(original));
	if (!private_callback_enter())
		return CB_ERR;
	if (!seeded_and_disjoint(boot, sizeof(*boot)) ||
	    classify_retained() != CB_SUCCESS)
		goto fail;
	candidate = (struct payload_mm_authvar_mor_linear_boot) {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LINEAR_REVISION,
		.size = sizeof(candidate),
		.kind = platform.boot_kind == STARBOOK_MTL_MOR_BOOT_COLD ?
			PAYLOAD_MM_AUTHVAR_MOR_LINEAR_COLD_BOOT :
			PAYLOAD_MM_AUTHVAR_MOR_LINEAR_S3_RESUME,
		.generation = platform.boot_kind == STARBOOK_MTL_MOR_BOOT_COLD ?
			platform.generation : 0,
	};
	*boot = candidate;
	if (!private_callback_leave(true)) {
		memcpy(boot, original, sizeof(original));
		goto fail_without_leave;
	}
	return CB_SUCCESS;
fail:
	(void)private_callback_leave(false);
fail_without_leave:
	cleanup_failure();
	provider_terminal_poison();
	return CB_ERR;
}

static enum cb_err reservations_register(void *context)
{
	struct mtl_mor_platform *state = context;
	struct mtl_mor_platform *frozen = NULL;

	if (state != &platform)
		return CB_ERR;
	if (!private_callback_enter())
		return CB_ERR;
	if (platform.reserved) {
		(void)private_callback_leave(true);
		return CB_ERR;
	}
	frozen = scratch_claim(MTL_MOR_SCRATCH_RESERVATIONS);
	if (!frozen || platform.boot_kind != STARBOOK_MTL_MOR_BOOT_COLD)
		goto fail;
	if (starbook_mtl_mor_clear_x86_register(&platform.reservations) !=
	    CB_SUCCESS)
		goto fail;
	platform_snapshot(frozen);
	if (platform.private.reservations_register(platform.private.context,
		platform.generation) != CB_SUCCESS || !platform_matches(frozen))
		goto fail;
	if (!scratch_release(MTL_MOR_SCRATCH_RESERVATIONS)) {
		frozen = NULL;
		goto fail;
	}
	frozen = NULL;
	platform.reserved = 1U;
	if (!private_callback_leave(true)) {
		platform.reserved = 0;
		goto fail_without_leave;
	}
	return CB_SUCCESS;
fail:
	(void)private_callback_leave(false);
fail_without_leave:
	platform.reserved = 0;
	scrub(&platform.reservations, sizeof(platform.reservations));
	if (frozen)
		scratch_abort(MTL_MOR_SCRATCH_RESERVATIONS);
	cleanup_failure();
	provider_terminal_poison();
	return CB_ERR;
}

static enum cb_err resolve_binding(void *context, uint64_t generation,
	struct payload_mm_authvar_mor_clear_plan *plan,
	struct payload_mm_authvar_mor_clear_executor_ops *executor)
{
	struct mtl_mor_platform *state = context;
	struct mtl_mor_platform *frozen = NULL;
	uint8_t plan_original[sizeof(*plan)] = { 0 };
	uint8_t executor_original[sizeof(*executor)] = { 0 };
	struct bootmem_aligned_reservation transport = { 0 };
	bool outputs_owned = false;

	if (state != &platform ||
	    !provider_object_valid(plan, sizeof(*plan), _Alignof(*plan)) ||
	    !provider_object_valid(executor, sizeof(*executor),
		_Alignof(*executor)) ||
	    ranges_overlap(plan, sizeof(*plan), executor, sizeof(*executor)))
		return CB_ERR;
	if (!private_callback_enter())
		return CB_ERR;
	if (platform.resolved) {
		(void)private_callback_leave(true);
		scrub(&plan_original, sizeof(plan_original));
		scrub(&executor_original, sizeof(executor_original));
		return CB_ERR;
	}
#if ENV_TEST
	starbook_mtl_mor_platform_resolve_preflight_test_hook(&platform.private);
#endif
	if (!private_context_disjoint(plan, sizeof(*plan)) ||
	    !private_context_disjoint(executor, sizeof(*executor)) ||
	    !private_context_disjoint(plan_original, sizeof(plan_original)) ||
	    !private_context_disjoint(executor_original, sizeof(executor_original)) ||
	    !private_context_disjoint(&transport, sizeof(transport)) ||
	    platform.reserved != 1U ||
	    bytes_nonzero(&platform.binding, sizeof(platform.binding)) ||
	    bytes_nonzero(&platform.transport, sizeof(platform.transport)) ||
	    __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE) !=
		MTL_MOR_SEEDED ||
	    generation != platform.generation)
		goto fail;
	frozen = scratch_claim(MTL_MOR_SCRATCH_RESOLVE);
	if (!frozen)
		goto fail;
	memcpy(plan_original, plan, sizeof(plan_original));
	memcpy(executor_original, executor, sizeof(executor_original));
	memset(plan, 0, sizeof(*plan));
	memset(executor, 0, sizeof(*executor));
	outputs_owned = true;
	platform_snapshot(frozen);
	if (platform.private.resolve(platform.private.context, generation,
		platform.owner, &transport) != CB_SUCCESS)
		goto fail;
#if ENV_TEST
	starbook_mtl_mor_platform_resolve_return_test_hook(&platform.private);
#endif
	if (!platform_matches(frozen) || !transport_valid(&transport))
		goto fail;
	platform.transport = transport;
	if (starbook_mtl_mor_clear_x86_prepare(&platform.reservations,
		&platform.guard, &platform.transport, false, plan,
		&platform.binding) != CB_SUCCESS ||
	    memcmp(&platform.transport, &transport, sizeof(transport)) ||
	    memcmp(plan, &platform.binding.authority.plan, sizeof(*plan)))
		goto fail;
	*executor = platform.binding.ops;
	platform.resolved = 1U;
#if ENV_TEST
	starbook_mtl_mor_platform_resolve_release_test_hook();
#endif
	if (!scratch_release(MTL_MOR_SCRATCH_RESOLVE)) {
		frozen = NULL;
		goto fail;
	}
	frozen = NULL;
	if (!private_callback_leave(true))
		goto fail_without_leave;
	scrub(&plan_original, sizeof(plan_original));
	scrub(&executor_original, sizeof(executor_original));
	scrub(&transport, sizeof(transport));
	return CB_SUCCESS;
fail:
	(void)private_callback_leave(false);
fail_without_leave:
	cleanup_failure();
	if (outputs_owned) {
		memcpy(plan, plan_original, sizeof(plan_original));
		memcpy(executor, executor_original, sizeof(executor_original));
	}
	platform.resolved = 0;
	scrub(&platform.binding, sizeof(platform.binding));
	scrub(&platform.transport, sizeof(platform.transport));
	if (frozen)
		scratch_abort(MTL_MOR_SCRATCH_RESOLVE);
	provider_terminal_poison();
	scrub(&plan_original, sizeof(plan_original));
	scrub(&executor_original, sizeof(executor_original));
	scrub(&transport, sizeof(transport));
	return CB_ERR;
}

static enum cb_err private_complete(void *context,
	const struct payload_mm_authvar_mor_grant *grant)
{
	struct mtl_mor_platform *state = context;
	struct mtl_mor_platform *frozen = NULL;
	enum cb_err status;

	if (state != &platform ||
	    !provider_object_valid(grant, sizeof(*grant), _Alignof(*grant)))
		return CB_ERR;
	if (!private_callback_enter())
		return CB_ERR;
	if (!seeded_and_disjoint(grant, sizeof(*grant)) ||
	    platform.resolved != 1U)
		goto fail;
	frozen = scratch_claim(MTL_MOR_SCRATCH_COMPLETE);
	if (!frozen)
		goto fail;
	platform_snapshot(frozen);
	status = platform.private.complete(platform.private.context, grant);
	if (status != CB_SUCCESS || !platform_matches(frozen))
		goto fail;
	if (cleanup_take(false) != CB_SUCCESS)
		goto fail;
	if (!scratch_release(MTL_MOR_SCRATCH_COMPLETE)) {
		frozen = NULL;
		goto fail;
	}
	frozen = NULL;
	if (!private_callback_terminal_leave())
		goto fail;
	return CB_SUCCESS;
fail:
	(void)private_callback_leave(false);
	if (frozen)
		scratch_abort(MTL_MOR_SCRATCH_COMPLETE);
	cleanup_failure();
	provider_terminal_poison();
	return CB_ERR;
}

static enum cb_err private_close(void *context)
{
	struct mtl_mor_platform *state = context;
	enum cb_err status;
	uint32_t expected = MTL_MOR_CALLBACK_IDLE;

	if (state != &platform)
		return CB_ERR;
	if (!__atomic_compare_exchange_n(&platform.callback_state, &expected,
		MTL_MOR_CALLBACK_ACTIVE, false, __ATOMIC_ACQ_REL,
		__ATOMIC_ACQUIRE) && expected != MTL_MOR_CALLBACK_POISONED)
		return CB_ERR;
	status = cleanup_take(true);
	provider_terminal_poison();
	return status;
}

void platform_payload_mm_authvar_smm_arena_abort(void)
{
	if (private_close(&platform) != CB_SUCCESS)
		provider_terminal_poison();
}

bool platform_payload_mm_authvar_mor_linear_ops(
	struct payload_mm_authvar_mor_linear_ops *ops)
{
	struct payload_mm_authvar_mor_linear_ops candidate;
	uint8_t original[sizeof(*ops)];
	bool private_seed_live = false;

	if (!provider_object_valid(ops, sizeof(*ops), _Alignof(*ops)))
		return false;
	memcpy(original, ops, sizeof(original));
#if ENV_TEST
	starbook_mtl_mor_platform_constructor_scratch_test_hook(&candidate,
		sizeof(candidate), &original, sizeof(original));
#endif
	if (!private_callback_enter())
		return false;
	if (ensure_seed(ops, sizeof(*ops), &candidate, sizeof(candidate),
		&original, sizeof(original)) != CB_SUCCESS)
		goto fail;
	private_seed_live = true;
	if (!seeded_and_disjoint(ops, sizeof(*ops)))
		goto fail;
	candidate = (struct payload_mm_authvar_mor_linear_ops) {
		.context = &platform,
		.context_size = sizeof(platform),
		.classify_guard = classify_guard,
		.reservations_register = reservations_register,
		.resolve_binding = resolve_binding,
		.private_complete = private_complete,
		.private_close = private_close,
	};
	*ops = candidate;
	if (!private_callback_leave(true)) {
		memcpy(ops, original, sizeof(original));
		goto fail_without_leave;
	}
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	return true;
fail:
	(void)private_callback_leave(false);
fail_without_leave:
	if (private_seed_live)
		close_private_seed();
	provider_terminal_poison();
	scrub(&candidate, sizeof(candidate));
	scrub(&original, sizeof(original));
	return false;
}

#if ENV_TEST
void starbook_mtl_mor_platform_reset_test(void)
{
	memset(&platform, 0, sizeof(platform));
	memset(&scratch, 0, sizeof(scratch));
	memset(&cleanup, 0, sizeof(cleanup));
}

bool starbook_mtl_mor_platform_poisoned_test(void)
{
	return __atomic_load_n(&platform.seed_state, __ATOMIC_ACQUIRE) ==
		MTL_MOR_SEED_POISONED;
}

bool starbook_mtl_mor_platform_owner_zero_test(void)
{
	return !bytes_nonzero(platform.owner, sizeof(platform.owner));
}

bool starbook_mtl_mor_platform_scratch_zero_test(void)
{
	return !bytes_nonzero(&scratch.frozen, sizeof(scratch.frozen));
}

bool starbook_mtl_mor_platform_scratch_idle_test(void)
{
	return __atomic_load_n(&scratch.owner, __ATOMIC_ACQUIRE) ==
		MTL_MOR_SCRATCH_IDLE;
}

bool starbook_mtl_mor_platform_scratch_exact_gap_test(void)
{
	uint8_t *limit = (uint8_t *)&scratch + sizeof(scratch);

	return !ranges_overlap(limit, 1, &scratch, sizeof(scratch)) &&
		ranges_overlap(limit - 1, 1, &scratch, sizeof(scratch));
}

bool starbook_mtl_mor_platform_scratch_contend_test(void)
{
	return !scratch_claim(MTL_MOR_SCRATCH_CLOSE);
}

void *starbook_mtl_mor_platform_storage_test(bool scratch_storage)
{
	return scratch_storage ? (void *)&scratch : (void *)&platform;
}

void starbook_mtl_mor_platform_cleanup_mutate_test(bool mirror)
{
	uint8_t *bytes = (uint8_t *)(mirror ? &cleanup.mirror : &cleanup.authority);

	bytes[0] ^= 1U;
}
#endif
