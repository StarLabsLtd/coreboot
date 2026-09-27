/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/smm_invocation_loader_instance.h"

#define assert(condition) do { if (!(condition)) __builtin_trap(); } while (0)

void starbook_mtl_loader_instance_test_set_attempt(uint32_t attempt);

static void test_protected_span(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	const struct starbook_mtl_loader_instance_owner *owner,
	uintptr_t *base, size_t *size, uint64_t *limit)
{
	const uintptr_t fanout_base = (uintptr_t)fanout;
	const uintptr_t owner_base = (uintptr_t)owner;
	const uintptr_t first = fanout_base < owner_base ? fanout_base : owner_base;
	const uintptr_t fanout_end = fanout_base + sizeof(*fanout);
	const uintptr_t owner_end = owner_base + sizeof(*owner);
	const uintptr_t end = fanout_end > owner_end ? fanout_end : owner_end;

	*base = first;
	*size = end - first;
	*limit = end;
}

static enum cb_err test_fanout_begin(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t ignored_base, size_t ignored_size, uint64_t ignored_limit,
	struct starbook_mtl_loader_instance_owner *owner)
{
	uintptr_t base;
	size_t size;
	uint64_t limit;

	(void)ignored_base;
	(void)ignored_size;
	(void)ignored_limit;
	test_protected_span(fanout, owner, &base, &size, &limit);
	return starbook_mtl_loader_instance_fanout_begin(fanout, base, size,
		limit, owner);
}

static enum cb_err raw_fanout_begin(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t base, size_t size, uint64_t limit,
	struct starbook_mtl_loader_instance_owner *owner)
{
	return starbook_mtl_loader_instance_fanout_begin(fanout, base, size,
		limit, owner);
}

static enum cb_err raw_fanout_commit(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	uint32_t lifecycle,
	struct smm_invocation_loader_instance_nonce nonce,
	struct starbook_mtl_loader_instance_owner *owner)
{
	return starbook_mtl_loader_instance_fanout_commit(fanout,
		protected_base, protected_size, protected_limit, lifecycle, nonce,
		owner);
}

static enum cb_err test_fanout_commit(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t ignored_base, size_t ignored_size, uint64_t ignored_limit,
	uint32_t lifecycle,
	struct smm_invocation_loader_instance_nonce nonce,
	struct starbook_mtl_loader_instance_owner *owner)
{
	(void)ignored_base;
	(void)ignored_size;
	(void)ignored_limit;
	return raw_fanout_commit(fanout, (uintptr_t)fanout->protected_base,
		(size_t)fanout->protected_size, fanout->protected_limit, lifecycle,
		nonce, owner);
}

static enum cb_err test_fanout_take(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t ignored_base, size_t ignored_size, uint64_t ignored_limit,
	struct smm_invocation_loader_instance_seed *seed)
{
	(void)ignored_base;
	(void)ignored_size;
	(void)ignored_limit;
	return starbook_mtl_loader_instance_fanout_take(fanout,
		(uintptr_t)fanout->protected_base, (size_t)fanout->protected_size,
		fanout->protected_limit, seed);
}

static enum cb_err test_fanout_read_legacy(
	const struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t ignored_base, size_t ignored_size, uint64_t ignored_limit,
	uint32_t *lifecycle, uint64_t *generation)
{
	(void)ignored_base;
	(void)ignored_size;
	(void)ignored_limit;
	return starbook_mtl_loader_instance_fanout_read_legacy(fanout,
		(uintptr_t)fanout->protected_base, (size_t)fanout->protected_size,
		fanout->protected_limit, lifecycle, generation);
}

static void raw_fanout_abort(
	struct starbook_mtl_loader_instance_fanout *fanout,
	uintptr_t protected_base, size_t protected_size, uint64_t protected_limit,
	struct starbook_mtl_loader_instance_owner *owner)
{
	starbook_mtl_loader_instance_fanout_abort(fanout, protected_base,
		protected_size, protected_limit, owner);
}

static void test_fanout_abort(
	struct starbook_mtl_loader_instance_fanout *fanout,
	struct starbook_mtl_loader_instance_owner *owner)
{
	raw_fanout_abort(fanout, (uintptr_t)owner->protected_base,
		(size_t)owner->protected_size, owner->protected_limit, owner);
}

#define starbook_mtl_loader_instance_fanout_begin test_fanout_begin
#define starbook_mtl_loader_instance_fanout_commit test_fanout_commit
#define starbook_mtl_loader_instance_fanout_take test_fanout_take
#define starbook_mtl_loader_instance_fanout_read_legacy test_fanout_read_legacy
#define starbook_mtl_loader_instance_fanout_abort test_fanout_abort

static bool poison_commit;
static uint64_t requiesce_limit;
static enum cb_err requiesce_result;
static unsigned int requiesce_calls;

static enum cb_err test_requiesce(uint64_t *protected_limit)
{
	requiesce_calls++;
	*protected_limit = requiesce_limit;
	if (requiesce_result != CB_SUCCESS)
		return requiesce_result;
	return CB_SUCCESS;
}

void starbook_mtl_loader_instance_test_hook(uint32_t point,
	struct starbook_mtl_loader_instance_fanout *fanout)
{
	if (point == 2U && poison_commit)
		__atomic_store_n(&fanout->state,
			STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED,
			__ATOMIC_RELEASE);
}

static bool tail_zero(const struct starbook_mtl_loader_instance_fanout *fanout)
{
	const uint8_t *bytes = (const uint8_t *)fanout;
	uint8_t value = 0;

	for (size_t index = sizeof(fanout->state); index < sizeof(*fanout); index++)
		value |= bytes[index];
	return !value;
}

static void test_valid(void)
{
	struct starbook_mtl_loader_instance_fanout fanout = { 0 };
	struct starbook_mtl_loader_instance_owner owner = { 0 };
	struct starbook_mtl_loader_instance_owner stale_owner;
	struct smm_invocation_loader_instance_seed seed;
	const struct smm_invocation_loader_instance_nonce nonce = {
		.low = 0x123456789abcdef0ULL,
		.high = 0xfedcba9876543210ULL,
	};
	uint32_t lifecycle;
	uint64_t generation;
	const uint64_t limit = (uintptr_t)&fanout + sizeof(fanout);

	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_SUCCESS);
	stale_owner = owner;
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD, nonce, &owner) == CB_SUCCESS);
	assert(!memcmp(&owner,
		&(const struct starbook_mtl_loader_instance_owner) { 0 },
		sizeof(owner)));
	starbook_mtl_loader_instance_fanout_abort(&fanout, &stale_owner);
	assert(fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED);
	assert(stale_owner.attempt != 0);
	assert(starbook_mtl_loader_instance_fanout_read_legacy(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &lifecycle,
		&generation) == CB_SUCCESS);
	assert(lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD &&
		generation == nonce.low);
	assert(starbook_mtl_loader_instance_fanout_read_legacy(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &lifecycle,
		&generation) == CB_SUCCESS);
	assert(starbook_mtl_loader_instance_fanout_take(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &seed) == CB_SUCCESS);
	assert(seed.revision == SMM_INVOCATION_LOADER_INSTANCE_REVISION &&
		seed.size == sizeof(seed) &&
		seed.lifecycle == SMM_INVOCATION_LOADER_NON_S3_LOAD &&
		smm_invocation_loader_instance_nonce_equal(
			seed.loader_instance_nonce, nonce));
	assert(fanout.loader_state == STARBOOK_MTL_LOADER_INSTANCE_TAKE_CONSUMED &&
		smm_invocation_loader_instance_nonce_is_zero(
			fanout.loader_instance_nonce));
	memset(&seed, 0xa5, sizeof(seed));
	assert(starbook_mtl_loader_instance_fanout_take(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &seed) == CB_ERR);
	assert(!memcmp(&seed,
		&(const struct smm_invocation_loader_instance_seed) { 0 },
		sizeof(seed)));
	assert(starbook_mtl_loader_instance_fanout_read_legacy(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &lifecycle,
		&generation) == CB_SUCCESS && generation == nonce.low);
}

static void test_failures(void)
{
	struct starbook_mtl_loader_instance_fanout fanout = { 0 };
	struct starbook_mtl_loader_instance_owner owner = { 0 };
	struct starbook_mtl_loader_instance_fanout saved;
	const struct smm_invocation_loader_instance_nonce nonce = { .low = 1 };
	const uint64_t limit = (uintptr_t)&fanout + sizeof(fanout);

	((uint8_t *)&fanout)[sizeof(fanout) - 1U] = 1;
	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_ERR);
	assert(fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
		tail_zero(&fanout));
	memset(&fanout, 0, sizeof(fanout));
	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_SUCCESS);
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { 0 }, &owner) == CB_ERR);
	assert(fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
		tail_zero(&fanout));
	memset(&fanout, 0, sizeof(fanout));
	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_SUCCESS);
	starbook_mtl_loader_instance_fanout_abort(&fanout, &owner);
	assert(fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
		tail_zero(&fanout));
	memset(&fanout, 0, sizeof(fanout));
	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_SUCCESS);
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit,
		SMM_INVOCATION_LOADER_S3_RELOAD, nonce, &owner) == CB_SUCCESS);
	saved = fanout;
	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_ERR);
	assert(!memcmp(&saved, &fanout, sizeof(saved)));
	assert(starbook_mtl_loader_instance_fanout_take(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit,
		(struct smm_invocation_loader_instance_seed *)&fanout) == CB_ERR_ARG);
}

static void test_ranges(void)
{
	struct starbook_mtl_loader_instance_fanout fanout = { 0 };
	struct starbook_mtl_loader_instance_owner owner = { 0 };
	struct {
		struct starbook_mtl_loader_instance_fanout fanout;
		struct starbook_mtl_loader_instance_owner owner;
	} fixture = { 0 };
	const uint64_t limit = (uintptr_t)&fanout + sizeof(fanout);
	const uintptr_t fixture_base = (uintptr_t)&fixture;
	const size_t owner_offset = (uintptr_t)&fixture.owner - fixture_base;

	assert(raw_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit - 1U, &owner) == CB_ERR_ARG);
	assert(raw_fanout_begin(&fanout,
		(uintptr_t)&fanout + 1U, sizeof(fanout), limit + 1U,
		&owner) == CB_ERR_ARG);
	assert(raw_fanout_begin(&fanout,
		UINTPTR_MAX - 1U, sizeof(fanout), UINT64_MAX, &owner) == CB_ERR_ARG);
	assert(raw_fanout_begin(&fixture.fanout, fixture_base,
		owner_offset + sizeof(fixture.owner),
		(uintptr_t)&fixture.owner + sizeof(fixture.owner) - 1U,
		&fixture.owner) == CB_ERR_ARG);
	assert(raw_fanout_begin(&fixture.fanout, fixture_base,
		owner_offset + sizeof(fixture.owner) - 1U,
		fixture_base + sizeof(fixture), &fixture.owner) == CB_ERR_ARG);
}

static void test_commit_ambiguity(void)
{
	struct starbook_mtl_loader_instance_fanout fanout = { 0 };
	struct starbook_mtl_loader_instance_owner owner = { 0 };
	const uint64_t limit = (uintptr_t)&fanout + sizeof(fanout);

	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_SUCCESS);
	poison_commit = true;
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 1U },
		&owner) == CB_ERR);
	poison_commit = false;
	assert(fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
		tail_zero(&fanout));
	assert(!memcmp(&owner,
		&(const struct starbook_mtl_loader_instance_owner) { 0 },
		sizeof(owner)));
}

static void test_attempt_wrap(void)
{
	struct starbook_mtl_loader_instance_fanout fanout = { 0 };
	struct starbook_mtl_loader_instance_owner owner = { 0 };
	const uint64_t limit = (uintptr_t)&fanout + sizeof(fanout);

	starbook_mtl_loader_instance_test_set_attempt(UINT32_MAX);
	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), limit, &owner) == CB_ERR);
	assert(fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
		tail_zero(&fanout));
	assert(!memcmp(&owner,
		&(const struct starbook_mtl_loader_instance_owner) { 0 },
		sizeof(owner)));
	starbook_mtl_loader_instance_test_set_attempt(0);
}

static void test_owner_mutation_is_inert(void)
{
	struct {
		struct starbook_mtl_loader_instance_fanout fanout;
		struct starbook_mtl_loader_instance_owner owner;
	} first = { 0 }, second = { 0 }, third = { 0 };
	struct starbook_mtl_loader_instance_fanout saved;
	struct starbook_mtl_loader_instance_owner mutated;

	assert(raw_fanout_begin(&first.fanout, (uintptr_t)&first, sizeof(first),
		(uintptr_t)&first + sizeof(first), &first.owner) == CB_SUCCESS);
	first.owner.fanout_identity = (uintptr_t)&second.fanout;
	saved = first.fanout;
	mutated = first.owner;
	assert(starbook_mtl_loader_instance_fanout_commit(&first.fanout,
		(uintptr_t)&first, sizeof(first), (uintptr_t)&first + sizeof(first),
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 1U },
		&first.owner) == CB_ERR_ARG);
	assert(!memcmp(&saved, &first.fanout, sizeof(saved)) &&
		!memcmp(&mutated, &first.owner, sizeof(mutated)));
	assert(raw_fanout_begin(&second.fanout, (uintptr_t)&second,
		sizeof(second), (uintptr_t)&second + sizeof(second),
		&second.owner) == CB_SUCCESS);
	second.owner.attempt++;
	saved = second.fanout;
	mutated = second.owner;
	starbook_mtl_loader_instance_fanout_abort(&second.fanout, &second.owner);
	assert(!memcmp(&saved, &second.fanout, sizeof(saved)) &&
		!memcmp(&mutated, &second.owner, sizeof(mutated)));
	assert(raw_fanout_begin(&third.fanout, (uintptr_t)&third, sizeof(third),
		(uintptr_t)&third + sizeof(third), &third.owner) == CB_SUCCESS);
	third.owner.attempt++;
	saved = third.fanout;
	mutated = third.owner;
	assert(starbook_mtl_loader_instance_fanout_commit(&third.fanout,
		(uintptr_t)&third, sizeof(third), (uintptr_t)&third + sizeof(third),
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 1U },
		&third.owner) == CB_ERR_ARG);
	assert(!memcmp(&saved, &third.fanout, sizeof(saved)) &&
		!memcmp(&mutated, &third.owner, sizeof(mutated)));
}

static void test_range_mutation_poison(void)
{
	struct authority {
		struct starbook_mtl_loader_instance_fanout fanout;
		struct starbook_mtl_loader_instance_owner owner;
	} fixtures[13] = { 0 };
	const struct smm_invocation_loader_instance_nonce nonce = { .low = 1U };

	for (size_t index = 0; index < 13U; index++)
		assert(raw_fanout_begin(&fixtures[index].fanout,
			(uintptr_t)&fixtures[index], sizeof(fixtures[index]),
			(uintptr_t)&fixtures[index] + sizeof(fixtures[index]),
			&fixtures[index].owner) == CB_SUCCESS);
	fixtures[0].fanout.protected_base++;
	fixtures[1].fanout.protected_size--;
	fixtures[2].fanout.protected_limit--;
	for (size_t index = 0; index < 3U; index++) {
		struct authority *fixture = &fixtures[index];

		assert(raw_fanout_commit(&fixture->fanout,
			(uintptr_t)fixture, sizeof(*fixture),
			(uintptr_t)fixture + sizeof(*fixture),
			SMM_INVOCATION_LOADER_NON_S3_LOAD, nonce,
			&fixture->owner) == CB_ERR);
		assert(fixture->fanout.state ==
			STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
			tail_zero(&fixture->fanout));
	}
	for (size_t index = 3U; index < 7U; index++) {
		const uintptr_t base = (uintptr_t)&fixtures[index];
		const size_t size = sizeof(fixtures[index]);
		const uint64_t limit = base + size;

		assert(raw_fanout_commit(&fixtures[index].fanout,
			base - (index == 3U), size - (index == 4U),
			limit - (index == 5U) + (index == 6U),
			SMM_INVOCATION_LOADER_NON_S3_LOAD, nonce,
			&fixtures[index].owner) == CB_ERR);
		assert(fixtures[index].fanout.state ==
			STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
			tail_zero(&fixtures[index].fanout));
	}
	fixtures[7].fanout.protected_base++;
	fixtures[8].fanout.protected_size--;
	fixtures[9].fanout.protected_limit--;
	for (size_t index = 7U; index < 10U; index++) {
		struct authority *fixture = &fixtures[index];

		raw_fanout_abort(&fixture->fanout, (uintptr_t)fixture,
			sizeof(*fixture), (uintptr_t)fixture + sizeof(*fixture),
			&fixture->owner);
		assert(fixture->fanout.state ==
			STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
			tail_zero(&fixture->fanout));
	}
	for (size_t index = 10U; index < 13U; index++) {
		const uintptr_t base = (uintptr_t)&fixtures[index];
		const size_t size = sizeof(fixtures[index]);
		const uint64_t limit = base + size;

		raw_fanout_abort(&fixtures[index].fanout,
			base + (index == 10U), size - (index == 11U),
			limit - (index == 12U), &fixtures[index].owner);
		assert(fixtures[index].fanout.state ==
			STARBOOK_MTL_LOADER_INSTANCE_FANOUT_POISONED &&
			tail_zero(&fixtures[index].fanout));
	}
}

static void test_requiesce_boundary(void)
{
	struct {
		struct starbook_mtl_loader_instance_fanout fanout;
		struct starbook_mtl_loader_instance_owner owner;
		struct smm_invocation_loader_instance_seed seed;
	} fixture = { 0 };
	struct starbook_mtl_loader_instance_fanout saved;
	const uintptr_t base = (uintptr_t)&fixture;
	const size_t size = sizeof(fixture);

	requiesce_limit = base + size;
	requiesce_result = CB_SUCCESS;
	requiesce_calls = 0;
	assert(raw_fanout_begin(&fixture.fanout,
		base, size, requiesce_limit, &fixture.owner) == CB_SUCCESS);
	assert(starbook_mtl_loader_instance_fanout_commit(&fixture.fanout,
		base, size, requiesce_limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) {
			.low = 5U, .high = 6U,
		}, &fixture.owner) == CB_SUCCESS);
	saved = fixture.fanout;
	requiesce_result = CB_ERR;
	memset(&fixture.seed, 0xa5, sizeof(fixture.seed));
	assert(starbook_mtl_loader_instance_fanout_take_requiesced(
		&fixture.fanout, base, size, &fixture.seed,
		test_requiesce) == CB_ERR);
	assert(requiesce_calls == 1U &&
		!memcmp(&saved, &fixture.fanout, sizeof(saved)));
	assert(!memcmp(&fixture.seed,
		&(const struct smm_invocation_loader_instance_seed) { 0 },
		sizeof(fixture.seed)));
	requiesce_result = CB_SUCCESS;
	assert(starbook_mtl_loader_instance_fanout_take_requiesced(
		&fixture.fanout, base, size, &fixture.seed,
		test_requiesce) == CB_SUCCESS);
	assert(requiesce_calls == 2U &&
		fixture.seed.loader_instance_nonce.low == 5U &&
		fixture.seed.loader_instance_nonce.high == 6U);
}

struct begin_call {
	struct starbook_mtl_loader_instance_fanout *fanout;
	uintptr_t base;
	size_t size;
	uint64_t limit;
	pthread_barrier_t *barrier;
	struct starbook_mtl_loader_instance_owner owner;
	enum cb_err result;
};

static void *begin_thread(void *argument)
{
	struct begin_call *call = argument;
	const int barrier_result = pthread_barrier_wait(call->barrier);

	assert(!barrier_result || barrier_result == PTHREAD_BARRIER_SERIAL_THREAD);
	call->result = raw_fanout_begin(call->fanout,
		call->base, call->size, call->limit,
		&call->owner);
	return NULL;
}

static void test_single_owner(void)
{
	struct starbook_mtl_loader_instance_fanout fanout = { 0 };
	struct starbook_mtl_loader_instance_fanout saved;
	pthread_barrier_t barrier;
	pthread_t threads[2];
	struct begin_call calls[2];
	struct starbook_mtl_loader_instance_owner forged;
	struct starbook_mtl_loader_instance_owner clone;
	struct starbook_mtl_loader_instance_fanout other = { 0 };
	unsigned int successes = 0;
	size_t winner;
	size_t loser;
	uintptr_t protected_base;
	uintptr_t protected_end;

	assert(!pthread_barrier_init(&barrier, NULL, 2));
	protected_base = (uintptr_t)&fanout;
	protected_end = protected_base + sizeof(fanout);
#define INCLUDE_PROTECTED(object) do { \
	const uintptr_t object_base = (uintptr_t)&(object); \
	const uintptr_t object_end = object_base + sizeof(object); \
	if (object_base < protected_base) \
		protected_base = object_base; \
	if (object_end > protected_end) \
		protected_end = object_end; \
} while (0)
	INCLUDE_PROTECTED(calls);
	INCLUDE_PROTECTED(forged);
	INCLUDE_PROTECTED(clone);
	INCLUDE_PROTECTED(other);
#undef INCLUDE_PROTECTED
	for (size_t index = 0; index < 2; index++) {
		calls[index] = (struct begin_call) {
			.fanout = &fanout,
			.base = protected_base,
			.size = protected_end - protected_base,
			.limit = protected_end,
			.barrier = &barrier,
		};
		assert(!pthread_create(&threads[index], NULL, begin_thread,
			&calls[index]));
	}
	for (size_t index = 0; index < 2; index++) {
		assert(!pthread_join(threads[index], NULL));
		successes += calls[index].result == CB_SUCCESS;
	}
	assert(successes == 1U &&
		fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_PROVISIONING);
	assert((calls[0].owner.attempt != 0) != (calls[1].owner.attempt != 0));
	winner = calls[0].result == CB_SUCCESS ? 0U : 1U;
	loser = winner ^ 1U;
	saved = fanout;
	clone = calls[winner].owner;
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), calls[winner].limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 3U },
		&clone) == CB_ERR_ARG);
	assert(!memcmp(&saved, &fanout, sizeof(saved)));
	clone = calls[winner].owner;
	forged = clone;
	starbook_mtl_loader_instance_fanout_abort(&fanout, &clone);
	assert(!memcmp(&saved, &fanout, sizeof(saved)));
	assert(!memcmp(&clone, &forged, sizeof(clone)));
	forged = calls[winner].owner;
	forged.fanout_identity = (uintptr_t)&other;
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), calls[winner].limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 3U },
		&forged) == CB_ERR_ARG);
	assert(!memcmp(&saved, &fanout, sizeof(saved)));
	forged = calls[winner].owner;
	forged.attempt++;
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), calls[winner].limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 3U },
		&forged) == CB_ERR_ARG);
	assert(!memcmp(&saved, &fanout, sizeof(saved)));
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), calls[loser].limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 3U },
		&calls[loser].owner) == CB_ERR_ARG);
	assert(!memcmp(&saved, &fanout, sizeof(saved)));
	starbook_mtl_loader_instance_fanout_abort(&fanout,
		&calls[loser].owner);
	assert(!memcmp(&saved, &fanout, sizeof(saved)));
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), calls[winner].limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) { .low = 3U },
		&calls[winner].owner) == CB_SUCCESS);
	assert(fanout.state == STARBOOK_MTL_LOADER_INSTANCE_FANOUT_COMMITTED);
	assert(!pthread_barrier_destroy(&barrier));
}

struct consume_call {
	struct starbook_mtl_loader_instance_fanout *fanout;
	uint64_t limit;
	pthread_barrier_t *barrier;
};

static void *take_thread(void *argument)
{
	struct consume_call *call = argument;
	struct smm_invocation_loader_instance_seed seed;
	const int barrier_result = pthread_barrier_wait(call->barrier);

	assert(!barrier_result || barrier_result == PTHREAD_BARRIER_SERIAL_THREAD);
	assert(starbook_mtl_loader_instance_fanout_take(call->fanout,
		(uintptr_t)call->fanout, sizeof(*call->fanout), call->limit,
		&seed) == CB_SUCCESS);
	return NULL;
}

static void *legacy_thread(void *argument)
{
	struct consume_call *call = argument;
	const int barrier_result = pthread_barrier_wait(call->barrier);
	uint32_t lifecycle;
	uint64_t generation;

	assert(!barrier_result || barrier_result == PTHREAD_BARRIER_SERIAL_THREAD);
	for (size_t index = 0; index < 1000U; index++)
		assert(starbook_mtl_loader_instance_fanout_read_legacy(call->fanout,
			(uintptr_t)call->fanout, sizeof(*call->fanout), call->limit,
			&lifecycle, &generation) == CB_SUCCESS && generation == 7U);
	return NULL;
}

static void test_parallel_consumers(void)
{
	struct starbook_mtl_loader_instance_fanout fanout = { 0 };
	struct starbook_mtl_loader_instance_owner owner = { 0 };
	pthread_barrier_t barrier;
	pthread_t take;
	pthread_t legacy;
	struct consume_call call = {
		.fanout = &fanout,
		.limit = (uintptr_t)&fanout + sizeof(fanout),
		.barrier = &barrier,
	};

	assert(starbook_mtl_loader_instance_fanout_begin(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), call.limit, &owner) == CB_SUCCESS);
	assert(starbook_mtl_loader_instance_fanout_commit(&fanout,
		(uintptr_t)&fanout, sizeof(fanout), call.limit,
		SMM_INVOCATION_LOADER_NON_S3_LOAD,
		(struct smm_invocation_loader_instance_nonce) {
			.low = 7U, .high = 8U,
		}, &owner) == CB_SUCCESS);
	assert(!pthread_barrier_init(&barrier, NULL, 2));
	assert(!pthread_create(&take, NULL, take_thread, &call));
	assert(!pthread_create(&legacy, NULL, legacy_thread, &call));
	assert(!pthread_join(take, NULL));
	assert(!pthread_join(legacy, NULL));
	assert(!pthread_barrier_destroy(&barrier));
}

int main(void)
{
	test_valid();
	test_failures();
	test_ranges();
	test_commit_ambiguity();
	test_attempt_wrap();
	test_owner_mutation_is_inert();
	test_range_mutation_poison();
	test_requiesce_boundary();
	test_single_owner();
	test_parallel_consumers();
	return 0;
}
