/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <bootmem.h>
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)
#define MAX_RANGES 40U

struct source_range {
	uint64_t base;
	uint64_t size;
	unsigned long tag;
};

static struct source_range sources[MAX_RANGES];
static size_t source_count;
static void *mutate;

bool bootmem_walk_dram(range_action_t action, void *argument)
{
	for (size_t index = 0; index < source_count; index++) {
		struct range_entry range;

		if (mutate) {
			((uint8_t *)mutate)[0] ^= 1;
			mutate = NULL;
		}
		range_entry_init(&range, sources[index].base,
			sources[index].base + sources[index].size,
			sources[index].tag);
		if (!action(&range, argument))
			return true;
	}
	return false;
}

static struct payload_mm_authvar_mor_live_inventory_request request(void)
{
	struct payload_mm_authvar_mor_live_inventory_request value = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_REVISION,
		.size = sizeof(value),
		.generation = 7,
		.identity = { 0x77 },
		.overlay_count = 2,
		.overlays = {
			{
				.base = 0x2000,
				.size = 0x1000,
				.exclusion_reason =
					PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
			},
			{
				.base = 0x5000,
				.size = 0x1000,
				.exclusion_reason =
					PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
			},
		},
	};

	return value;
}

static void set_success_sources(void)
{
	/* The gap at [0x8000, 0x9000) is not authoritative DRAM. */
	sources[0] = (struct source_range) { 0x1000, 0x7000, BM_MEM_RAM };
	sources[1] = (struct source_range) { 0x9000, 0x1000, BM_MEM_RAMSTAGE };
	sources[2] = (struct source_range) { 0xa000, 0x1000, BM_MEM_RESERVED };
	source_count = 3;
}

static void expect_zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;

	for (size_t index = 0; index < size; index++)
		CHECK(!bytes[index]);
}

static void success(void)
{
	struct payload_mm_authvar_mor_live_inventory_request input = request();
	struct payload_mm_authvar_mor_clear_plan plan;

	set_success_sources();
	memset(&plan, 0xa5, sizeof(plan));
	CHECK(payload_mm_authvar_mor_live_inventory_compose(&input, &plan) ==
		CB_SUCCESS);
	CHECK(plan.inventory_generation == input.generation && plan.span_count == 7);
	CHECK(!memcmp(plan.inventory_identity, input.identity,
		sizeof(plan.inventory_identity)));
	CHECK(plan.spans[0].base == 0x1000 && plan.spans[0].size == 0x1000 &&
		plan.spans[0].span_class ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED);
	CHECK(plan.spans[1].base == 0x2000 &&
		plan.spans[1].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE);
	CHECK(plan.spans[3].base == 0x5000 &&
		plan.spans[3].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED);
	CHECK(plan.spans[5].base == 0x9000 &&
		plan.spans[5].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE);
	CHECK(plan.spans[6].base == 0xa000 &&
		plan.spans[6].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED);
}

static void failures(void)
{
	struct payload_mm_authvar_mor_live_inventory_request input = request();
	struct payload_mm_authvar_mor_clear_plan plan;

#define FAIL(statement) do { \
	set_success_sources(); input = request(); statement; \
	memset(&plan, 0xa5, sizeof(plan)); \
	CHECK(payload_mm_authvar_mor_live_inventory_compose(&input, &plan) != \
		CB_SUCCESS); expect_zero(&plan, sizeof(plan)); \
} while (0)
	FAIL(input.overlays[0].base = 0);
	FAIL(input.overlays[0].base = 0x7000; input.overlays[0].size = 0x3000);
	FAIL(input.overlays[1].base = 0x2800);
	FAIL(input.overlays[1].base = UINT64_MAX; input.overlays[1].size = 2);
	FAIL(input.overlays[0].reserved = 1);
	FAIL(input.overlays[2].base = 1);
	FAIL(sources[1].base = 0x7000);
	FAIL(sources[0].size = UINT64_MAX);
	FAIL(sources[2].tag = BM_MEM_LAST);
	FAIL(mutate = &input);
	FAIL(mutate = &plan);
#undef FAIL
}

static void boundaries(void)
{
	struct payload_mm_authvar_mor_live_inventory_request input = request();
	struct payload_mm_authvar_mor_clear_plan plan;

	input.overlay_count = 1;
	input.overlays[0].base = UINT64_MAX - 2;
	input.overlays[0].size = 1;
	memset(&input.overlays[1], 0, sizeof(input.overlays[1]));
	sources[0] = (struct source_range) {
		UINT64_MAX - 3, 3, BM_MEM_RAM,
	};
	source_count = 1;
	CHECK(payload_mm_authvar_mor_live_inventory_compose(&input, &plan) ==
		CB_SUCCESS);
	CHECK(plan.span_count == 3 &&
		plan.spans[2].base == UINT64_MAX - 1 && plan.spans[2].size == 1);

	input = request();
	input.overlay_count = 1;
	input.overlays[0] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = 0x1000,
		.size = 0x1000,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	memset(&input.overlays[1], 0, sizeof(input.overlays[1]));
	for (size_t index = 0; index < 15; index++)
		sources[index] = (struct source_range) {
			.base = 0x1000 + index * 0x1000,
			.size = 0x1000,
			.tag = index & 1U ? BM_MEM_RESERVED : BM_MEM_RAM,
		};
	source_count = 15;
	CHECK(payload_mm_authvar_mor_live_inventory_compose(&input, &plan) ==
		CB_SUCCESS && plan.span_count == 15);
	sources[15] = (struct source_range) { 0x10000, 0x1000, BM_MEM_RESERVED };
	source_count = 16;
	CHECK(payload_mm_authvar_mor_live_inventory_compose(&input, &plan) !=
		CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
}

#if defined(MOCK_PLAN_BUILDER)
static size_t expected_raw_count;
static size_t builder_calls;

enum cb_err payload_mm_authvar_mor_clear_plan_build(
	const struct payload_mm_authvar_mor_clear_inventory *inventory,
	struct payload_mm_authvar_mor_clear_plan *plan)
{
	builder_calls++;
	CHECK(inventory->span_count == expected_raw_count);
	memset(plan, 0, sizeof(*plan));
	plan->revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION;
	plan->size = sizeof(*plan);
	plan->inventory_generation = inventory->generation;
	memcpy(plan->inventory_identity, inventory->identity,
		sizeof(plan->inventory_identity));
	plan->span_count = 1;
	plan->spans[0] = inventory->spans[0];
	return CB_SUCCESS;
}

static void raw_count(void)
{
	struct payload_mm_authvar_mor_live_inventory_request input = request();
	struct payload_mm_authvar_mor_clear_plan plan;

	input.overlay_count = 1;
	input.overlays[0] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
		.base = 0x1000,
		.size = 0x1000,
		.exclusion_reason =
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
	};
	memset(&input.overlays[1], 0, sizeof(input.overlays[1]));
	for (size_t index = 0; index < MAX_RANGES; index++)
		sources[index] = (struct source_range) {
			.base = 0x1000 + index * 0x2000,
			.size = 0x1000,
			.tag = BM_MEM_RAM,
		};
	source_count = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS;
	expected_raw_count = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_RAW_MAX_SPANS;
	CHECK(payload_mm_authvar_mor_live_inventory_compose(&input, &plan) ==
		CB_SUCCESS && builder_calls == 1);
	source_count++;
	builder_calls = 0;
	memset(&plan, 0xa5, sizeof(plan));
	CHECK(payload_mm_authvar_mor_live_inventory_compose(&input, &plan) !=
		CB_SUCCESS && !builder_calls);
	expect_zero(&plan, sizeof(plan));
}
#endif

int main(int argc, char **argv)
{
	if (argc == 1) {
		success();
		failures();
		boundaries();
		return 0;
	}
	CHECK(argc == 2);
	if (!strcmp(argv[1], "success"))
		success();
	else if (!strcmp(argv[1], "failures"))
		failures();
	else if (!strcmp(argv[1], "boundaries"))
		boundaries();
#if defined(MOCK_PLAN_BUILDER)
	else if (!strcmp(argv[1], "raw-count"))
		raw_count();
#endif
	else
		__builtin_trap();
	return 0;
}
