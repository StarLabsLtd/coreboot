/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_guard.h"

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static struct payload_mm_authvar_mor_clear_plan valid_plan(void)
{
	struct payload_mm_authvar_mor_clear_plan plan = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION,
		.size = sizeof(plan),
		.inventory_generation = 1,
		.inventory_identity = { 1 },
		.span_count = 4,
		.spans = {
			{ 0x1000, 0x1000, PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE },
			{ 0x9000, 0x9000, PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE },
			{ 0x12000, 0x3000, PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_EXCLUDED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED },
			{ 0x30000, 0x1000, PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
			  PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_NONE },
		},
	};

	return plan;
}

static struct starbook_mtl_dma_guard_snapshot valid_snapshot(void)
{
	struct starbook_mtl_dma_guard_snapshot snapshot = {
		.revision = STARBOOK_MTL_DMA_GUARD_REVISION,
		.size = sizeof(snapshot),
		.generation = 1,
		.identity = { 1 },
		.engine_count = STARBOOK_MTL_DMA_GUARD_ENGINES,
		.arena_count = STARBOOK_MTL_DMA_GUARD_ARENAS,
		.engines = {
			{ .base = 0xfc801000, .root = 0x11000,
			  .status = (1U << 30) | (1U << 31),
			  .mode = STARBOOK_MTL_DMA_GUARD_DEFAULT_DENY_TRANSLATION },
			{ .base = 0xfc800000,
			  .mode = STARBOOK_MTL_DMA_GUARD_BME_QUIESCED },
		},
		.handoff = { 0x10000, 0x1000 },
		.table = { 0x11000, 0x1000 },
		.table_mirror = { 0x9000, 0x1000 },
		.arenas = {
			{ 0x12000, 0x1000 }, { 0x13000, 0x1000 },
			{ 0x14000, 0x1000 },
		},
	};

	return snapshot;
}

static struct starbook_mtl_dma_guard_facts valid_facts(void)
{
	const struct starbook_mtl_dma_guard_snapshot snapshot = valid_snapshot();
	struct starbook_mtl_dma_guard_facts facts = {
		.live_buffer = { 0x10000, 0x5000 },
		.current_fsp_buffer = { 0x10000, 0x5000 },
		.table_mirror = snapshot.table_mirror,
		.current_cbmem_mirror = snapshot.table_mirror,
		.handoff = snapshot.handoff,
		.table = snapshot.table,
		.gfxvtbar = snapshot.engines[1].base | 1U,
		.tables_match = true,
		.integrated_requesters_verified = true,
	};

	memcpy(facts.engines, snapshot.engines, sizeof(facts.engines));
	memcpy(facts.arenas, snapshot.arenas, sizeof(facts.arenas));
	return facts;
}

static void snapshot_builder_tests(void)
{
	struct starbook_mtl_dma_guard_facts facts = valid_facts();
	struct starbook_mtl_dma_guard_snapshot snapshot;
	struct starbook_mtl_dma_guard_snapshot expected = valid_snapshot();
	union {
		struct starbook_mtl_dma_guard_facts facts;
		struct starbook_mtl_dma_guard_snapshot snapshot;
	} alias;

	expected.generation = 0;
	memset(expected.identity, 0, sizeof(expected.identity));
	CHECK(starbook_mtl_dma_guard_snapshot_build(&facts, &snapshot) == CB_SUCCESS);
	CHECK(!memcmp(&snapshot, &expected, sizeof(snapshot)));

#define REJECT_MUTATION(statement) do { \
	struct starbook_mtl_dma_guard_facts changed = facts; \
	memset(&snapshot, 0xa5, sizeof(snapshot)); \
	statement; \
	CHECK(starbook_mtl_dma_guard_snapshot_build(&changed, &snapshot) != \
		CB_SUCCESS); \
	CHECK(!memcmp(&snapshot, \
		&(const struct starbook_mtl_dma_guard_snapshot) { 0 }, \
		sizeof(snapshot))); \
} while (0)
	REJECT_MUTATION(changed.current_fsp_buffer.base++);
	REJECT_MUTATION(changed.current_fsp_buffer.size++);
	REJECT_MUTATION(changed.current_cbmem_mirror.base++);
	REJECT_MUTATION(changed.current_cbmem_mirror.size++);
	REJECT_MUTATION(changed.current_cbmem_mirror.size--);
	REJECT_MUTATION(changed.live_buffer.base++);
	REJECT_MUTATION(changed.live_buffer.size++);
	REJECT_MUTATION(changed.table_mirror.base = changed.live_buffer.base);
	REJECT_MUTATION(changed.table_mirror.size++);
	REJECT_MUTATION(changed.handoff.base++);
	REJECT_MUTATION(changed.handoff.size++);
	REJECT_MUTATION(changed.table.base++);
	REJECT_MUTATION(changed.table.size++);
	REJECT_MUTATION(changed.arenas[0].base++);
	REJECT_MUTATION(changed.arenas[0].size++);
	REJECT_MUTATION(changed.arenas[1].base++);
	REJECT_MUTATION(changed.arenas[1].size++);
	REJECT_MUTATION(changed.arenas[2].base++);
	REJECT_MUTATION(changed.arenas[2].size++);
	REJECT_MUTATION(changed.engines[0].base++);
	REJECT_MUTATION(changed.engines[0].root += 0x1000);
	REJECT_MUTATION(changed.engines[0].status ^= 1U << 31);
	REJECT_MUTATION(changed.engines[0].protected_memory_enable = 1U);
	REJECT_MUTATION(changed.engines[0].mode++);
	REJECT_MUTATION(changed.engines[1].base += 0x1000);
	REJECT_MUTATION(changed.engines[1].mode++);
	REJECT_MUTATION(changed.gfxvtbar ^= 1U);
	REJECT_MUTATION(changed.gfxvtbar += 0x1000);
	REJECT_MUTATION(changed.tables_match = false);
	REJECT_MUTATION(changed.integrated_requesters_verified = false);
#undef REJECT_MUTATION

	memset(&snapshot, 0xa5, sizeof(snapshot));
	CHECK(starbook_mtl_dma_guard_snapshot_build(NULL, &snapshot) == CB_ERR_ARG);
	CHECK(!memcmp(&snapshot,
		&(const struct starbook_mtl_dma_guard_snapshot) { 0 },
		sizeof(snapshot)));
	CHECK(starbook_mtl_dma_guard_snapshot_build(&facts, NULL) == CB_ERR_ARG);
	memset(&snapshot, 0xa5, sizeof(snapshot));
	CHECK(starbook_mtl_dma_guard_snapshot_build(
		(const struct starbook_mtl_dma_guard_facts *)((uintptr_t)&facts + 1U),
		&snapshot) == CB_ERR_ARG);
	CHECK(!memcmp(&snapshot,
		&(const struct starbook_mtl_dma_guard_snapshot) { 0 },
		sizeof(snapshot)));
	CHECK(starbook_mtl_dma_guard_snapshot_build(&facts,
		(struct starbook_mtl_dma_guard_snapshot *)((uintptr_t)&snapshot + 1U)) ==
		CB_ERR_ARG);
	memset(&snapshot, 0xa5, sizeof(snapshot));
	CHECK(starbook_mtl_dma_guard_snapshot_build(
		(const struct starbook_mtl_dma_guard_facts *)(uintptr_t)-8,
		&snapshot) == CB_ERR_ARG);
	CHECK(!memcmp(&snapshot,
		&(const struct starbook_mtl_dma_guard_snapshot) { 0 },
		sizeof(snapshot)));
	memset(&alias, 0xa5, sizeof(alias));
	CHECK(starbook_mtl_dma_guard_snapshot_build(&alias.facts,
		&alias.snapshot) == CB_ERR_ARG);
	CHECK(!memcmp(&alias.snapshot,
		&(const struct starbook_mtl_dma_guard_snapshot) { 0 },
		sizeof(alias.snapshot)));
}

struct mock_context {
	unsigned int observes;
	unsigned int ensures;
	unsigned int randoms;
	unsigned int poisons;
	bool ensure_failure;
	bool observe_failure;
	bool mutate_hardware;
	bool invalid_observation;
	bool random_failure;
	bool zero_generation;
	bool zero_identity;
	struct payload_mm_authvar_mor_clear_plan *plan;
	struct starbook_mtl_dma_guard_snapshot *output;
	bool mutate_plan;
	bool mutate_output;
	bool mutate_ops;
	struct starbook_mtl_dma_guard_ops *ops;
};

static enum cb_err mock_ensure(void *context)
{
	struct mock_context *mock = context;

	mock->ensures++;
	return mock->ensure_failure ? CB_ERR : CB_SUCCESS;
}

static enum cb_err mock_observe(void *context,
	struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	struct mock_context *mock = context;

	mock->observes++;
	if (mock->observe_failure)
		return CB_ERR;
	*snapshot = valid_snapshot();
	snapshot->generation = 0;
	memset(snapshot->identity, 0, sizeof(snapshot->identity));
	if (mock->invalid_observation)
		snapshot->engines[0].root += 0x1000;
	if (mock->mutate_hardware && mock->observes == 2U)
		snapshot->engines[0].status ^= 1U;
	if (mock->mutate_plan && mock->observes == 2U)
		mock->plan->inventory_generation++;
	if (mock->mutate_output && mock->observes == 2U)
		mock->output->reserved[0] = 1;
	if (mock->mutate_ops && mock->observes == 2U)
		mock->ops->random64 = NULL;
	return CB_SUCCESS;
}

static enum cb_err mock_random64(void *context, uint64_t *value)
{
	struct mock_context *mock = context;

	mock->randoms++;
	if (mock->random_failure)
		return CB_ERR;
	if ((mock->zero_generation && mock->randoms == 1U) ||
	    (mock->zero_identity && mock->randoms > 1U))
		*value = 0;
	else
		*value = 0x100U + mock->randoms;
	return CB_SUCCESS;
}

static void mock_poison(void *context)
{
	((struct mock_context *)context)->poisons++;
}

int main(int argc, char **argv)
{
	struct payload_mm_authvar_mor_clear_plan plan = valid_plan();
	struct starbook_mtl_dma_guard_snapshot snapshot = valid_snapshot();
	struct starbook_mtl_dma_guard_snapshot second;
	struct mock_context mock = { .plan = &plan, .output = &snapshot };
	struct starbook_mtl_dma_guard_ops ops = {
		.context = &mock, .ensure = mock_ensure, .observe = mock_observe,
		.random64 = mock_random64,
		.poison = mock_poison,
	};
	mock.ops = &ops;

	CHECK(argc == 2);
	if (!strcmp(argv[1], "snapshot-builder")) {
		snapshot_builder_tests();
		return 0;
	}
	if (!strcmp(argv[1], "ops-output-alias")) {
		CHECK(starbook_mtl_dma_guard_capture_with_ops(&plan, &snapshot,
			(const struct starbook_mtl_dma_guard_ops *)&snapshot) == CB_ERR_ARG);
		CHECK(!memcmp(&snapshot,
			&(const struct starbook_mtl_dma_guard_snapshot) { 0 },
			sizeof(snapshot)));
		return 0;
	}
	if (!strcmp(argv[1], "ops-plan-alias")) {
		CHECK(starbook_mtl_dma_guard_capture_with_ops(&plan, &snapshot,
			(const struct starbook_mtl_dma_guard_ops *)&plan) == CB_ERR_ARG);
		return 0;
	}
	if (!strcmp(argv[1], "capture") || !strcmp(argv[1], "idempotent")) {
		CHECK(starbook_mtl_dma_guard_capture_with_ops(&plan, &snapshot, &ops) ==
			CB_SUCCESS);
		CHECK(snapshot.generation && snapshot.identity[0]);
		CHECK(mock.ensures == 1U && mock.observes == 2U && mock.randoms == 5U);
		if (!strcmp(argv[1], "idempotent")) {
			CHECK(starbook_mtl_dma_guard_capture_with_ops(&plan, &second, &ops) ==
				CB_SUCCESS);
			CHECK(!memcmp(&snapshot, &second, sizeof(snapshot)));
			CHECK(mock.ensures == 2U && mock.observes == 4U &&
				mock.randoms == 5U);
		}
		return 0;
	}
	if (!strcmp(argv[1], "ensure-failure"))
		mock.ensure_failure = true;
	else if (!strcmp(argv[1], "observe-failure"))
		mock.observe_failure = true;
	else if (!strcmp(argv[1], "hardware-mutation"))
		mock.mutate_hardware = true;
	else if (!strcmp(argv[1], "invalid-observation"))
		mock.invalid_observation = true;
	else if (!strcmp(argv[1], "random-failure"))
		mock.random_failure = true;
	else if (!strcmp(argv[1], "zero-generation"))
		mock.zero_generation = true;
	else if (!strcmp(argv[1], "zero-identity"))
		mock.zero_identity = true;
	else if (!strcmp(argv[1], "plan-mutation"))
		mock.mutate_plan = true;
	else if (!strcmp(argv[1], "output-mutation"))
		mock.mutate_output = true;
	else if (!strcmp(argv[1], "ops-mutation"))
		mock.mutate_ops = true;
	else if (strcmp(argv[1], "policy"))
		CHECK(false);
	if (strcmp(argv[1], "policy")) {
		memset(&snapshot, 0xa5, sizeof(snapshot));
		CHECK(starbook_mtl_dma_guard_capture_with_ops(&plan, &snapshot, &ops) !=
			CB_SUCCESS);
		CHECK(!memcmp(&snapshot,
			&(const struct starbook_mtl_dma_guard_snapshot) { 0 },
			sizeof(snapshot)));
		CHECK(mock.poisons == 1U);
		mock.ensure_failure = false;
		mock.observe_failure = false;
		mock.mutate_hardware = false;
		mock.random_failure = false;
		mock.zero_generation = false;
		mock.zero_identity = false;
		mock.mutate_plan = false;
		mock.mutate_output = false;
		mock.mutate_ops = false;
		ops.random64 = mock_random64;
		plan = valid_plan();
		CHECK(starbook_mtl_dma_guard_capture_with_ops(&plan, &snapshot, &ops) !=
			CB_SUCCESS);
		return 0;
	}

	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) == CB_SUCCESS);
	plan.spans[1].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	plan = valid_plan();
	plan.spans[2].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	plan = valid_plan();
	plan.spans[1].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED;
	plan.spans[2].exclusion_reason =
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	plan = valid_plan();
	snapshot.table.size = 0x2001;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.arenas[2].base = UINT64_MAX;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.identity[0] = 0;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.reserved[15] = 1;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.table = snapshot.handoff;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.handoff.size--;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.handoff.base = 0xf000;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.engines[0].root += 0x1000;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	snapshot = valid_snapshot();
	snapshot.arenas[1].base++;
	CHECK(starbook_mtl_dma_guard_policy_validate(&plan, &snapshot) != CB_SUCCESS);
	return 0;
}
