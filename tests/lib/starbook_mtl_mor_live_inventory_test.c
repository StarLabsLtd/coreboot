/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_live_inventory.h"

#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <commonlib/helpers.h>
#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static void *mutate;
static bool compose_fail;
static bool policy_fail;
static unsigned int compose_calls;
static unsigned int policy_calls;
static size_t expected_extra;

static struct payload_mm_authvar_mor_clear_plan composed_plan(
	const struct payload_mm_authvar_mor_live_inventory_request *request)
{
	struct payload_mm_authvar_mor_clear_plan plan = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_CLEAR_REVISION,
		.size = sizeof(plan),
		.inventory_generation = request->generation,
		.span_count = 1,
		.spans = { {
			.base = request->overlays[0].base,
			.size = request->overlays[0].size,
			.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
		} },
	};

	memcpy(plan.inventory_identity, request->identity,
		sizeof(plan.inventory_identity));
	return plan;
}

enum cb_err payload_mm_authvar_mor_live_inventory_compose(
	const struct payload_mm_authvar_mor_live_inventory_request *request,
	struct payload_mm_authvar_mor_clear_plan *plan)
{
	static const uint32_t reasons[] = {
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
		PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_PLATFORM_RESERVED,
	};

	compose_calls++;
	CHECK(request->revision == PAYLOAD_MM_AUTHVAR_MOR_LIVE_INVENTORY_REVISION &&
		request->size == sizeof(*request) && request->generation == 9 &&
		request->identity[0] == 0x99 &&
		request->overlay_count == 6 + expected_extra);
	for (size_t index = 0; index < 6; index++) {
		CHECK(request->overlays[index].base == 0x1000 + index * 0x1000);
		CHECK(request->overlays[index].size == 0x1000);
		CHECK(request->overlays[index].exclusion_reason == reasons[index]);
	}
	for (size_t index = 0; index < expected_extra; index++) {
		CHECK(request->overlays[6 + index].base ==
			0x10000 + index * 0x1000);
		CHECK(request->overlays[6 + index].size == 0x1000);
		CHECK(request->overlays[6 + index].exclusion_reason ==
			PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE);
	}
	if (mutate) {
		((uint8_t *)mutate)[0] ^= 1;
		mutate = NULL;
	}
	if (compose_fail)
		return CB_ERR;
	*plan = composed_plan(request);
	return CB_SUCCESS;
}

enum cb_err starbook_mtl_dma_guard_policy_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *snapshot)
{
	(void)plan;
	policy_calls++;
	CHECK(snapshot->generation == 9 && snapshot->identity[0] == 0x99);
	return policy_fail ? CB_ERR : CB_SUCCESS;
}

static struct starbook_mtl_dma_guard_snapshot prepared(void)
{
	struct starbook_mtl_dma_guard_snapshot snapshot = {
		.revision = STARBOOK_MTL_DMA_GUARD_REVISION,
		.size = sizeof(snapshot),
		.generation = 9,
		.identity = { 0x99 },
		.handoff = { 0x1000, 0x1000 },
		.table = { 0x2000, 0x1000 },
		.table_mirror = { 0x3000, 0x1000 },
		.arenas = {
			{ 0x4000, 0x1000 },
			{ 0x5000, 0x1000 },
			{ 0x6000, 0x1000 },
		},
	};

	return snapshot;
}

static void expect_zero(const void *object, size_t size)
{
	const uint8_t *bytes = object;

	for (size_t index = 0; index < size; index++)
		CHECK(!bytes[index]);
}

int main(void)
{
	struct starbook_mtl_dma_guard_snapshot snapshot = prepared();
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_clear_plan original;
	struct payload_mm_authvar_mor_live_inventory_overlay overlays[4];

	for (size_t index = 0; index < ARRAY_SIZE(overlays); index++)
		overlays[index] = (struct payload_mm_authvar_mor_live_inventory_overlay) {
			.base = 0x10000 + index * 0x1000,
			.size = 0x1000,
			.exclusion_reason =
				PAYLOAD_MM_AUTHVAR_MOR_GRANT_EXCLUSION_ACTIVE_FIRMWARE,
		};

	CHECK(starbook_mtl_mor_live_inventory_compose(&snapshot, &plan) ==
		CB_SUCCESS && compose_calls == 1 && policy_calls == 1);
	original = plan;
	CHECK(starbook_mtl_mor_live_inventory_validate(&snapshot, &plan) ==
		CB_SUCCESS && compose_calls == 2 && policy_calls == 2);
	expected_extra = ARRAY_SIZE(overlays);
	memset(&plan, 0, sizeof(plan));
	CHECK(starbook_mtl_mor_live_inventory_compose_with_overlays(&snapshot,
		overlays, ARRAY_SIZE(overlays), &plan) == CB_SUCCESS);
	mutate = overlays;
	memset(&plan, 0xa5, sizeof(plan));
	CHECK(starbook_mtl_mor_live_inventory_compose_with_overlays(&snapshot,
		overlays, ARRAY_SIZE(overlays), &plan) != CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	overlays[0].base = 0x10000;
	expected_extra = 0;
	plan.inventory_generation++;
	CHECK(starbook_mtl_mor_live_inventory_validate(&snapshot, &plan) !=
		CB_SUCCESS);
	plan = original;
	mutate = &snapshot;
	memset(&plan, 0xa5, sizeof(plan));
	CHECK(starbook_mtl_mor_live_inventory_compose(&snapshot, &plan) !=
		CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	snapshot = prepared();
	mutate = &plan;
	memset(&plan, 0xa5, sizeof(plan));
	CHECK(starbook_mtl_mor_live_inventory_compose(&snapshot, &plan) !=
		CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	compose_fail = true;
	memset(&plan, 0xa5, sizeof(plan));
	CHECK(starbook_mtl_mor_live_inventory_compose(&snapshot, &plan) !=
		CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	compose_fail = false;
	policy_fail = true;
	CHECK(starbook_mtl_mor_live_inventory_compose(&snapshot, &plan) !=
		CB_SUCCESS);
	expect_zero(&plan, sizeof(plan));
	return 0;
}
