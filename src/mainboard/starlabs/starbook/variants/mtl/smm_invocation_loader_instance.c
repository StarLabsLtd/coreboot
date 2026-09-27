/* SPDX-License-Identifier: GPL-2.0-only */

#include "smm_invocation_loader_instance.h"

#include <stdbool.h>
#include <string.h>

static uint32_t next_owner_attempt;

#ifdef __TEST__
void starbook_mtl_loader_instance_test_hook(uint32_t point,
	struct starbook_mtl_loader_instance_fanout *fanout);
void starbook_mtl_loader_instance_test_set_attempt(uint32_t attempt)
{
	__atomic_store_n(&next_owner_attempt, attempt, __ATOMIC_RELAXED);
}
#define LOADER_INSTANCE_TEST_HOOK(point, fanout) \
	starbook_mtl_loader_instance_test_hook(point, fanout)
#else
#define LOADER_INSTANCE_TEST_HOOK(point, fanout) do { } while (0)
#endif

static bool object_valid(const void *object, size_t size, size_t alignment)
{
	const uintptr_t base = (uintptr_t)object;

	return object && size && !(base % alignment) &&
		base <= (uintptr_t)-1 - (size - 1U);
}

static bool objects_overlap(const void *first, size_t first_size,
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

static bool range_protected(uintptr_t base, size_t size, uint64_t limit)
{
	return size && base <= (uintptr_t)-1 - (size - 1U) &&
		(uint64_t)base < limit && size <= limit - (uint64_t)base;
}

static bool object_in_range(const void *object, size_t object_size,
	uintptr_t range_base, size_t range_size)
{
	const uintptr_t object_base = (uintptr_t)object;

	return object_valid(object, object_size, 1) && range_size &&
		range_base <= (uintptr_t)-1 - (range_size - 1U) &&
		range_base <= object_base && object_base - range_base <= range_size &&
		object_size <= range_size - (object_base - range_base);
}

static bool lifecycle_valid(uint32_t lifecycle)
{
	return lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD ||
		lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD;
}

static bool fanout_range_valid(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit)
{
	return object_valid(fanout, sizeof(*fanout), _Alignof(*fanout)) &&
		range_protected(protected_base, protected_size, protected_limit) &&
		object_in_range(fanout, sizeof(*fanout), protected_base,
			protected_size);
}

static void owner_scrub(struct starbook_mtl_loader_instance_owner *owner)
{
	memset(owner, 0, sizeof(*owner));
}

static bool owner_sealed_range_valid(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	const struct starbook_mtl_loader_instance_owner *owner)
{
	return object_valid(owner, sizeof(*owner), _Alignof(*owner)) &&
		!objects_overlap(fanout, sizeof(*fanout), owner, sizeof(*owner)) &&
		owner->fanout_identity == (uint64_t)(uintptr_t)fanout &&
		owner->protected_base <= UINTPTR_MAX &&
		owner->protected_size <= SIZE_MAX &&
		fanout_range_valid(fanout, (uintptr_t)owner->protected_base,
			(size_t)owner->protected_size, owner->protected_limit) &&
		object_in_range(owner, sizeof(*owner),
			(uintptr_t)owner->protected_base,
			(size_t)owner->protected_size);
}

static bool owned_claim_valid(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	const struct starbook_mtl_loader_instance_owner *owner)
{
	return owner_sealed_range_valid(fanout, owner) && owner->attempt &&
		!owner->reserved &&
		owner->attempt == fanout->owner_attempt &&
		fanout->owner_identity == (uint64_t)(uintptr_t)owner;
}

static bool fanout_seal_matches_owner(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	const struct starbook_mtl_loader_instance_owner *owner)
{
	return owner->protected_base == fanout->protected_base &&
		owner->protected_size == fanout->protected_size &&
		owner->protected_limit == fanout->protected_limit;
}

static void poison_claimed(
	struct starbook_mtl_loader_instance_fanout *fanout,
	struct starbook_mtl_loader_instance_owner *owner)
{
	uint32_t state;
	volatile uint8_t *bytes = (volatile uint8_t *)fanout;

	state = __atomic_load_n(&fanout->state, __ATOMIC_ACQUIRE);
	while (state != STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
	       state != STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONING &&
	       !__atomic_compare_exchange_n(&fanout->state, &state,
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		;
	if (state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED ||
	    state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONING) {
		owner_scrub(owner);
		return;
	}
	for (size_t index = sizeof(fanout->state); index < sizeof(*fanout); index++)
		bytes[index] = 0;
	__atomic_store_n(&fanout->state,
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED, __ATOMIC_RELEASE);
	owner_scrub(owner);
}

static void poison_owned(
	struct starbook_mtl_loader_instance_fanout *fanout,
	struct starbook_mtl_loader_instance_owner *owner)
{
	if (owned_claim_valid(fanout, owner))
		poison_claimed(fanout, owner);
}

enum cb_err starbook_mtl_loader_instance_fanout_begin(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	struct starbook_mtl_loader_instance_owner *owner)
{
	const struct starbook_mtl_loader_instance_fanout empty = { 0 };
	uint32_t expected = STARBOOK_MTL_LOADER_INSTANCE_FANOUT_EMPTY;
	uint32_t attempt;

	if (!fanout_range_valid(fanout, protected_base, protected_size,
		protected_limit) ||
	    !object_valid(owner, sizeof(*owner), _Alignof(*owner)) ||
	    objects_overlap(fanout, sizeof(*fanout), owner, sizeof(*owner)) ||
	    !object_in_range(owner, sizeof(*owner), protected_base,
		protected_size) ||
	    memcmp(owner, &(const struct starbook_mtl_loader_instance_owner) { 0 },
		sizeof(*owner)))
		return CB_ERR_ARG;
	if (!__atomic_compare_exchange_n(&fanout->state, &expected,
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_PROVISIONING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	if (memcmp((const uint8_t *)fanout + sizeof(fanout->state),
		(const uint8_t *)&empty + sizeof(empty.state),
		sizeof(*fanout) - sizeof(fanout->state))) {
		struct starbook_mtl_loader_instance_owner dirty_owner = { 0 };

		poison_claimed(fanout, &dirty_owner);
		return CB_ERR;
	}
	attempt = __atomic_add_fetch(&next_owner_attempt, 1U, __ATOMIC_RELAXED);
	if (!attempt) {
		fanout->owner_attempt = 1U;
		*owner = (struct starbook_mtl_loader_instance_owner) {
			.fanout_identity = (uint64_t)(uintptr_t)fanout,
			.protected_base = protected_base,
			.protected_size = protected_size,
			.protected_limit = protected_limit,
			.attempt = 1U,
		};
		fanout->owner_identity = (uint64_t)(uintptr_t)owner;
		fanout->protected_base = protected_base;
		fanout->protected_size = protected_size;
		fanout->protected_limit = protected_limit;
		poison_owned(fanout, owner);
		return CB_ERR;
	}
	fanout->owner_attempt = attempt;
	fanout->owner_identity = (uint64_t)(uintptr_t)owner;
	fanout->protected_base = protected_base;
	fanout->protected_size = protected_size;
	fanout->protected_limit = protected_limit;
	*owner = (struct starbook_mtl_loader_instance_owner) {
		.fanout_identity = (uint64_t)(uintptr_t)fanout,
		.protected_base = protected_base,
		.protected_size = protected_size,
		.protected_limit = protected_limit,
		.attempt = attempt,
	};
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_loader_instance_fanout_commit(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	uint32_t lifecycle,
	struct smm_invocation_loader_instance_nonce loader_instance_nonce,
	struct starbook_mtl_loader_instance_owner *owner)
{
	struct starbook_mtl_loader_instance_owner trusted_owner;
	uint32_t expected = STARBOOK_MTL_LOADER_INSTANCE_FANOUT_PROVISIONING;

	if (!object_valid(fanout, sizeof(*fanout), _Alignof(*fanout)) ||
	    !object_valid(owner, sizeof(*owner), _Alignof(*owner)) ||
	    !owned_claim_valid(fanout, owner))
		return CB_ERR_ARG;
	trusted_owner = *owner;
	if (!fanout_range_valid(fanout, protected_base, protected_size,
		protected_limit) || protected_base != owner->protected_base ||
	    protected_size != owner->protected_size ||
	    protected_limit != owner->protected_limit) {
		poison_claimed(fanout, owner);
		return CB_ERR;
	}
	if (__atomic_load_n(&fanout->state, __ATOMIC_ACQUIRE) !=
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_PROVISIONING) {
		owner_scrub(owner);
		return CB_ERR;
	}
	if (!lifecycle_valid(lifecycle) || !loader_instance_nonce.low)
		goto fail;
	fanout->revision = STARBOOK_MTL_LOADER_INSTANCE_FANOUT_REVISION;
	fanout->size = sizeof(*fanout);
	fanout->loader_state = STARBOOK_MTL_LOADER_INSTANCE_TAKE_READY;
	fanout->lifecycle = lifecycle;
	fanout->reserved = 0;
	fanout->loader_instance_nonce = loader_instance_nonce;
	fanout->legacy_generation = loader_instance_nonce.low;
	fanout->reserved_owner = 0;
	fanout->reserved_tail = 0;
	if (memcmp(owner, &trusted_owner, sizeof(*owner)) ||
	    !owned_claim_valid(fanout, owner) ||
	    !fanout_seal_matches_owner(fanout, owner))
		goto fail;
	LOADER_INSTANCE_TEST_HOOK(1, fanout);
	if (memcmp(owner, &trusted_owner, sizeof(*owner)) ||
	    !owned_claim_valid(fanout, owner) ||
	    !fanout_seal_matches_owner(fanout, owner)) {
		poison_claimed(fanout, owner);
		return CB_ERR;
	}
	if (!__atomic_compare_exchange_n(&fanout->state, &expected,
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTING, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		poison_owned(fanout, owner);
		return CB_ERR;
	}
	fanout->owner_identity = 0;
	owner_scrub(owner);
	LOADER_INSTANCE_TEST_HOOK(2, fanout);
	expected = STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTING;
	if (!__atomic_compare_exchange_n(&fanout->state, &expected,
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED, false,
		__ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
		poison_claimed(fanout, owner);
		return CB_ERR;
	}
	return CB_SUCCESS;
fail:
	poison_owned(fanout, owner);
	return CB_ERR;
}

void starbook_mtl_loader_instance_fanout_abort(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	struct starbook_mtl_loader_instance_owner *owner)
{
	bool current_range_matches;

	if (object_valid(fanout, sizeof(*fanout), _Alignof(*fanout)) &&
	    object_valid(owner, sizeof(*owner), _Alignof(*owner))) {
		current_range_matches =
			fanout_range_valid(fanout, protected_base, protected_size,
				protected_limit) &&
			object_in_range(owner, sizeof(*owner), protected_base,
				protected_size) &&
			protected_base == owner->protected_base &&
			protected_size == owner->protected_size &&
			protected_limit == owner->protected_limit;
		if (owned_claim_valid(fanout, owner) &&
		    __atomic_load_n(&fanout->state, __ATOMIC_ACQUIRE) ==
			STARBOOK_MTL_LOADER_INSTANCE_FANOUT_PROVISIONING) {
			/* The protected owner seal remains the write authority when
			 * caller facts or the fanout's redundant seal are corrupted. */
			if (!current_range_matches ||
			    !fanout_seal_matches_owner(fanout, owner)) {
				poison_claimed(fanout, owner);
				return;
			}
			poison_claimed(fanout, owner);
		}
	}
}

enum cb_err starbook_mtl_loader_instance_fanout_take(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	struct smm_invocation_loader_instance_seed *seed)
{
	struct starbook_mtl_loader_instance_fanout copy;
	uint32_t expected = STARBOOK_MTL_LOADER_INSTANCE_TAKE_READY;
	enum cb_err result = CB_ERR;

	if (!fanout_range_valid(fanout, protected_base, protected_size,
		protected_limit) ||
	    !object_valid(seed, sizeof(*seed), _Alignof(*seed)) ||
	    objects_overlap(fanout, sizeof(*fanout), seed, sizeof(*seed)))
		return CB_ERR_ARG;
	memset(seed, 0, sizeof(*seed));
	if (__atomic_load_n(&fanout->state, __ATOMIC_ACQUIRE) !=
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED ||
	    !__atomic_compare_exchange_n(&fanout->loader_state, &expected,
		STARBOOK_MTL_LOADER_INSTANCE_TAKE_TAKING, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return CB_ERR;
	copy = *fanout;
	if (copy.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED &&
	    copy.revision == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_REVISION &&
	    copy.size == sizeof(copy) &&
	    copy.loader_state == STARBOOK_MTL_LOADER_INSTANCE_TAKE_TAKING &&
	    lifecycle_valid(copy.lifecycle) && copy.loader_instance_nonce.low &&
	    copy.legacy_generation == copy.loader_instance_nonce.low &&
	    !copy.reserved && copy.owner_attempt && !copy.reserved_owner &&
	    !copy.owner_identity && copy.protected_base == protected_base &&
	    copy.protected_size == protected_size &&
	    copy.protected_limit == protected_limit &&
	    !copy.reserved_tail &&
	    !memcmp(&copy, fanout, sizeof(copy))) {
		seed->revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
		seed->size = sizeof(*seed);
		seed->lifecycle = copy.lifecycle;
		seed->loader_instance_nonce = copy.loader_instance_nonce;
		result = CB_SUCCESS;
	}
	fanout->loader_instance_nonce.low = 0;
	fanout->loader_instance_nonce.high = 0;
	__atomic_store_n(&fanout->loader_state,
		STARBOOK_MTL_LOADER_INSTANCE_TAKE_CONSUMED, __ATOMIC_RELEASE);
	memset(&copy, 0, sizeof(copy));
	if (result != CB_SUCCESS)
		memset(seed, 0, sizeof(*seed));
	return result;
}

enum cb_err starbook_mtl_loader_instance_fanout_read_legacy(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	uint32_t *lifecycle, uint64_t *generation)
{
	uint32_t revision;
	uint32_t size;
	uint32_t saved_lifecycle;
	uint32_t reserved;
	uint32_t owner_attempt;
	uint32_t reserved_owner;
	uint64_t owner_identity;
	uint64_t saved_protected_base;
	uint64_t saved_protected_size;
	uint64_t saved_protected_limit;
	uint64_t saved_generation;
	uint64_t reserved_tail;

	if (!fanout_range_valid(fanout, protected_base, protected_size,
		protected_limit) ||
	    !object_valid(lifecycle, sizeof(*lifecycle), _Alignof(*lifecycle)) ||
	    !object_valid(generation, sizeof(*generation), _Alignof(*generation)) ||
	    objects_overlap(lifecycle, sizeof(*lifecycle), generation,
		sizeof(*generation)) ||
	    objects_overlap(fanout, sizeof(*fanout), lifecycle, sizeof(*lifecycle)) ||
	    objects_overlap(fanout, sizeof(*fanout), generation, sizeof(*generation)))
		return CB_ERR_ARG;
	*lifecycle = 0;
	*generation = 0;
	if (__atomic_load_n(&fanout->state, __ATOMIC_ACQUIRE) !=
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED)
		return CB_ERR;
	revision = fanout->revision;
	size = fanout->size;
	saved_lifecycle = fanout->lifecycle;
	reserved = fanout->reserved;
	owner_attempt = fanout->owner_attempt;
	reserved_owner = fanout->reserved_owner;
	owner_identity = fanout->owner_identity;
	saved_protected_base = fanout->protected_base;
	saved_protected_size = fanout->protected_size;
	saved_protected_limit = fanout->protected_limit;
	saved_generation = fanout->legacy_generation;
	reserved_tail = fanout->reserved_tail;
	if (revision != STARBOOK_MTL_LOADER_INSTANCE_FANOUT_REVISION ||
	    size != sizeof(*fanout) || !lifecycle_valid(saved_lifecycle) ||
	    !saved_generation || reserved || !owner_attempt || reserved_owner ||
	    owner_identity ||
	    saved_protected_base != protected_base ||
	    saved_protected_size != protected_size ||
	    saved_protected_limit != protected_limit ||
	    reserved_tail ||
	    __atomic_load_n(&fanout->state, __ATOMIC_ACQUIRE) !=
		STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED ||
	    revision != fanout->revision || size != fanout->size ||
	    saved_lifecycle != fanout->lifecycle || reserved != fanout->reserved ||
	    owner_attempt != fanout->owner_attempt ||
	    reserved_owner != fanout->reserved_owner ||
	    owner_identity != fanout->owner_identity ||
	    saved_protected_base != fanout->protected_base ||
	    saved_protected_size != fanout->protected_size ||
	    saved_protected_limit != fanout->protected_limit ||
	    saved_generation != fanout->legacy_generation ||
	    reserved_tail != fanout->reserved_tail)
		return CB_ERR;
	*lifecycle = saved_lifecycle;
	*generation = saved_generation;
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_loader_instance_fanout_take_requiesced(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size,
	struct smm_invocation_loader_instance_seed *seed,
	enum cb_err (*requiesce)(uint64_t *protected_limit))
{
	uint64_t protected_limit = 0;

	if (!requiesce || !object_valid(seed, sizeof(*seed), _Alignof(*seed)) ||
	    objects_overlap(seed, sizeof(*seed), fanout, sizeof(*fanout)))
		return CB_ERR_ARG;
	memset(seed, 0, sizeof(*seed));
	if (requiesce(&protected_limit) != CB_SUCCESS)
		return CB_ERR;
	if (!range_protected((uintptr_t)seed, sizeof(*seed), protected_limit))
		return CB_ERR_ARG;
	return starbook_mtl_loader_instance_fanout_take(fanout, protected_base,
		protected_size, protected_limit, seed);
}
