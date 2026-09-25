/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_guard.h"

#define CHECK(condition) do { \
	if (!(condition)) \
		__builtin_trap(); \
} while (0)

static void *pre_copy_context;
static size_t pre_copy_context_size;

void starbook_mtl_dma_guard_pre_copy_test_hook(
	const struct starbook_mtl_dma_guard_ops *ops)
{
	if (!pre_copy_context)
		return;
	((struct starbook_mtl_dma_guard_ops *)ops)->context = pre_copy_context;
	((struct starbook_mtl_dma_guard_ops *)ops)->context_size =
		pre_copy_context_size;
	pre_copy_context = NULL;
}

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
	unsigned int mutation_observe;
	struct payload_mm_authvar_mor_clear_plan *plan;
	struct starbook_mtl_dma_guard_snapshot *output;
	struct starbook_mtl_dma_guard_snapshot *prepared;
	struct payload_mm_authvar_mor_clear_dma_snapshot *dma;
	bool mutate_plan;
	bool mutate_prepared;
	bool mutate_output;
	bool mutate_dma;
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
	if (mock->mutate_hardware && mock->observes == mock->mutation_observe)
		snapshot->engines[0].status ^= 1U;
	if (mock->mutate_plan && mock->observes == mock->mutation_observe)
		mock->plan->inventory_generation++;
	if (mock->mutate_prepared && mock->observes == mock->mutation_observe)
		mock->prepared->reserved[0] = 1;
	if (mock->mutate_output && mock->observes == mock->mutation_observe)
		mock->output->reserved[0] = 1;
	if (mock->mutate_dma && mock->observes == mock->mutation_observe)
		mock->dma->reserved[0] = 1;
	if (mock->mutate_ops && mock->observes == mock->mutation_observe)
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
	struct starbook_mtl_dma_guard_snapshot bound;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma;
	struct starbook_mtl_dma_guard_bind_workspace bind_workspace;
	struct mock_context mock = {
		.mutation_observe = 2, .plan = &plan, .output = &snapshot,
		.prepared = &snapshot, .dma = &dma,
	};
	struct starbook_mtl_dma_guard_ops ops = {
		.context = &mock, .context_size = sizeof(mock),
		.ensure = mock_ensure, .observe = mock_observe,
		.random64 = mock_random64,
		.poison = mock_poison,
	};
	mock.ops = &ops;
	CHECK(argc == 2);
	if (!strcmp(argv[1], "seed-zero")) {
		CHECK(starbook_mtl_dma_guard_seed(0, snapshot.identity) != CB_SUCCESS);
		memset(snapshot.identity, 0, sizeof(snapshot.identity));
		CHECK(starbook_mtl_dma_guard_seed(1, snapshot.identity) != CB_SUCCESS);
		return 0;
	}
	if (!strcmp(argv[1], "seeded-prepare")) {
		uint8_t identity[32] = { 0x5a };

		CHECK(starbook_mtl_dma_guard_seed(0x1122334455667788ULL,
			identity) == CB_SUCCESS);
		CHECK(starbook_mtl_dma_guard_seed(0x1122334455667788ULL,
			identity) == CB_SUCCESS);
		CHECK(starbook_mtl_dma_guard_seed(0x1122334455667789ULL,
			identity) != CB_SUCCESS);
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) ==
			CB_SUCCESS);
		CHECK(snapshot.generation == 0x1122334455667788ULL);
		CHECK(!memcmp(snapshot.identity, identity, sizeof(identity)));
		CHECK(mock.randoms == 0);
		return 0;
	}
	if (!strcmp(argv[1], "early-seed-required")) {
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) !=
			CB_SUCCESS);
		CHECK(!mock.ensures && !mock.observes && !mock.randoms && !mock.poisons);
		return 0;
	}

	if (!strcmp(argv[1], "snapshot-builder")) {
		snapshot_builder_tests();
		return 0;
	}
	if (!strcmp(argv[1], "ops-output-alias")) {
		second = snapshot;
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot,
			(const struct starbook_mtl_dma_guard_ops *)&snapshot) == CB_ERR_ARG);
		CHECK(!memcmp(&snapshot, &second, sizeof(snapshot)));
		return 0;
	}
	if (!strcmp(argv[1], "ops-plan-alias")) {
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot,
			(const struct starbook_mtl_dma_guard_ops *)&snapshot) == CB_ERR_ARG);
		return 0;
	}
	if (!strcmp(argv[1], "prepare-context-output-alias")) {
		second = snapshot;
		ops.context = &snapshot;
		ops.context_size = sizeof(snapshot);
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) ==
			CB_ERR_ARG);
		CHECK(!memcmp(&snapshot, &second, sizeof(snapshot)));
		return 0;
	}
	if (!strcmp(argv[1], "prepare-context-ops-alias")) {
		second = snapshot;
		ops.context = &ops;
		ops.context_size = sizeof(ops);
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) ==
			CB_ERR_ARG);
		CHECK(!memcmp(&snapshot, &second, sizeof(snapshot)));
		return 0;
	}
	if (!strcmp(argv[1], "prepare-pre-copy-mutation")) {
		second = snapshot;
		pre_copy_context = &snapshot;
		pre_copy_context_size = sizeof(snapshot);
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) ==
			CB_ERR_ARG);
		CHECK(!memcmp(&snapshot, &second, sizeof(snapshot)) &&
			!mock.ensures && !mock.observes && !mock.randoms &&
			!mock.poisons);
		return 0;
	}
	if (!strcmp(argv[1], "prepare") || !strcmp(argv[1], "idempotent")) {
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) ==
			CB_SUCCESS);
		CHECK(snapshot.generation && snapshot.identity[0]);
		CHECK(mock.ensures == 1U && mock.observes == 2U && mock.randoms == 5U);
		if (!strcmp(argv[1], "idempotent")) {
			CHECK(starbook_mtl_dma_guard_prepare_with_ops(&second, &ops) ==
				CB_SUCCESS);
			CHECK(!memcmp(&snapshot, &second, sizeof(snapshot)));
			CHECK(mock.ensures == 2U && mock.observes == 4U &&
				mock.randoms == 5U);
		}
		return 0;
	}
	if (!strcmp(argv[1], "bind") || !strcmp(argv[1], "bind-idempotent") ||
	    !strcmp(argv[1], "bind-token-mismatch") ||
	    !strcmp(argv[1], "bind-plan-mutation") ||
	    !strcmp(argv[1], "bind-prepared-mutation") ||
	    !strcmp(argv[1], "bind-output-mutation") ||
	    !strcmp(argv[1], "bind-dma-mutation") ||
	    !strcmp(argv[1], "bind-ops-mutation") ||
	    !strcmp(argv[1], "bind-prepared-output-alias") ||
	    !strcmp(argv[1], "bind-output-dma-alias") ||
	    !strcmp(argv[1], "bind-plan-prepared-alias") ||
	    !strcmp(argv[1], "bind-context-plan-alias") ||
	    !strcmp(argv[1], "bind-context-prepared-alias") ||
	    !strcmp(argv[1], "bind-context-output-alias") ||
	    !strcmp(argv[1], "bind-context-dma-alias") ||
	    !strcmp(argv[1], "bind-context-ops-alias") ||
	    !strcmp(argv[1], "bind-context-workspace-alias") ||
	    !strcmp(argv[1], "bind-pre-copy-mutation") ||
	    !strcmp(argv[1], "bound-prepare-idempotent") ||
	    !strcmp(argv[1], "bound-plan-change")) {
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) ==
			CB_SUCCESS);
		plan.inventory_generation = snapshot.generation;
		memcpy(plan.inventory_identity, snapshot.identity,
			sizeof(plan.inventory_identity));
		mock.mutation_observe = 3;
		mock.output = &bound;
		if (!strcmp(argv[1], "bind-pre-copy-mutation")) {
			memset(&bound, 0xa5, sizeof(bound));
			memset(&dma, 0xa5, sizeof(dma));
			memset(&bind_workspace, 0xa5, sizeof(bind_workspace));
			pre_copy_context = &bound;
			pre_copy_context_size = sizeof(bound);
			CHECK(starbook_mtl_dma_guard_bind_with_ops_owned(&plan,
				&snapshot, &bound, &dma, &ops, &bind_workspace) ==
				CB_ERR_ARG);
			for (size_t index = 0; index < sizeof(bound); index++)
				CHECK(((const uint8_t *)&bound)[index] == 0xa5);
			for (size_t index = 0; index < sizeof(dma); index++)
				CHECK(((const uint8_t *)&dma)[index] == 0xa5);
			return 0;
		}
		if (!strcmp(argv[1], "bind-prepared-output-alias")) {
			second = snapshot;
			CHECK(starbook_mtl_dma_guard_bind_with_ops(&plan, &snapshot,
				&snapshot, &dma, &ops) == CB_ERR_ARG);
			CHECK(!memcmp(&snapshot, &second, sizeof(snapshot)));
			return 0;
		}
		if (!strcmp(argv[1], "bind-output-dma-alias")) {
			CHECK(starbook_mtl_dma_guard_bind_with_ops(&plan, &snapshot,
				&bound,
				(struct payload_mm_authvar_mor_clear_dma_snapshot *)&bound,
				&ops) == CB_ERR_ARG);
			return 0;
		}
		if (!strcmp(argv[1], "bind-plan-prepared-alias")) {
			CHECK(starbook_mtl_dma_guard_bind_with_ops(&plan,
				(const struct starbook_mtl_dma_guard_snapshot *)&plan,
				&bound, &dma, &ops) == CB_ERR_ARG);
			return 0;
		}
		if (!strncmp(argv[1], "bind-context-", 13)) {
			memset(&bound, 0xa5, sizeof(bound));
			memset(&dma, 0xa5, sizeof(dma));
			memset(&bind_workspace, 0xa5, sizeof(bind_workspace));
			if (!strcmp(argv[1], "bind-context-plan-alias")) {
				ops.context = &plan;
				ops.context_size = sizeof(plan);
			} else if (!strcmp(argv[1], "bind-context-prepared-alias")) {
				ops.context = &snapshot;
				ops.context_size = sizeof(snapshot);
			} else if (!strcmp(argv[1], "bind-context-output-alias")) {
				ops.context = &bound;
				ops.context_size = sizeof(bound);
			} else if (!strcmp(argv[1], "bind-context-dma-alias")) {
				ops.context = &dma;
				ops.context_size = sizeof(dma);
			} else if (!strcmp(argv[1], "bind-context-ops-alias")) {
				ops.context = &ops;
				ops.context_size = sizeof(ops);
			} else {
				ops.context = &bind_workspace;
				ops.context_size = sizeof(bind_workspace);
			}
			CHECK(starbook_mtl_dma_guard_bind_with_ops_owned(&plan,
				&snapshot, &bound, &dma, &ops, &bind_workspace) ==
				CB_ERR_ARG);
			for (size_t index = 0; index < sizeof(bound); index++)
				CHECK(((const uint8_t *)&bound)[index] == 0xa5);
			for (size_t index = 0; index < sizeof(dma); index++)
				CHECK(((const uint8_t *)&dma)[index] == 0xa5);
			return 0;
		}
		if (!strcmp(argv[1], "bind-token-mismatch"))
			plan.inventory_generation++;
		else if (!strcmp(argv[1], "bind-plan-mutation"))
			mock.mutate_plan = true;
		else if (!strcmp(argv[1], "bind-prepared-mutation"))
			mock.mutate_prepared = true;
		else if (!strcmp(argv[1], "bind-output-mutation"))
			mock.mutate_output = true;
		else if (!strcmp(argv[1], "bind-dma-mutation"))
			mock.mutate_dma = true;
		else if (!strcmp(argv[1], "bind-ops-mutation"))
			mock.mutate_ops = true;
		memset(&bound, 0xa5, sizeof(bound));
		memset(&dma, 0xa5, sizeof(dma));
		if (strcmp(argv[1], "bind") && strcmp(argv[1], "bind-idempotent") &&
		    strcmp(argv[1], "bound-prepare-idempotent") &&
		    strcmp(argv[1], "bound-plan-change")) {
			CHECK(starbook_mtl_dma_guard_bind_with_ops(&plan, &snapshot,
				&bound, &dma, &ops) != CB_SUCCESS);
			CHECK(!memcmp(&bound,
				&(const struct starbook_mtl_dma_guard_snapshot) { 0 },
				sizeof(bound)));
			CHECK(!memcmp(&dma,
				&(const struct payload_mm_authvar_mor_clear_dma_snapshot) { 0 },
				sizeof(dma)));
			CHECK(mock.poisons == 1U);
			CHECK(starbook_mtl_dma_guard_prepare_with_ops(&second, &ops) !=
				CB_SUCCESS);
			return 0;
		}
		CHECK(starbook_mtl_dma_guard_bind_with_ops(&plan, &snapshot,
			&bound, &dma, &ops) == CB_SUCCESS);
		CHECK(!memcmp(&bound, &snapshot, sizeof(bound)));
		CHECK(dma.generation == snapshot.generation);
		CHECK(!memcmp(dma.identity, snapshot.identity, sizeof(dma.identity)));
		if (!strcmp(argv[1], "bound-prepare-idempotent")) {
			CHECK(starbook_mtl_dma_guard_prepare_with_ops(&second, &ops) ==
				CB_SUCCESS);
			CHECK(!memcmp(&second, &snapshot, sizeof(second)));
			return 0;
		}
		if (!strcmp(argv[1], "bound-plan-change")) {
			plan.spans[0].size--;
			CHECK(starbook_mtl_dma_guard_bind_with_ops(&plan, &snapshot,
				&bound, &dma, &ops) != CB_SUCCESS);
			CHECK(mock.poisons == 1U);
			return 0;
		}
		if (!strcmp(argv[1], "bind-idempotent")) {
			memset(&bound, 0xa5, sizeof(bound));
			memset(&dma, 0xa5, sizeof(dma));
			CHECK(starbook_mtl_dma_guard_bind_with_ops(&plan, &snapshot,
				&bound, &dma, &ops) == CB_SUCCESS);
			CHECK(!memcmp(&bound, &snapshot, sizeof(bound)));
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
	else if (!strcmp(argv[1], "output-mutation"))
		mock.mutate_output = true;
	else if (!strcmp(argv[1], "ops-mutation"))
		mock.mutate_ops = true;
	else if (strcmp(argv[1], "policy"))
		CHECK(false);
	if (strcmp(argv[1], "policy")) {
		memset(&snapshot, 0xa5, sizeof(snapshot));
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) !=
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
		mock.mutate_output = false;
		mock.mutate_ops = false;
		ops.random64 = mock_random64;
		plan = valid_plan();
		CHECK(starbook_mtl_dma_guard_prepare_with_ops(&snapshot, &ops) !=
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
	snapshot.identity[1] = 1;
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
