/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_GUARD_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_GUARD_H

#include <boot/payload_mm_authvar_mor_clear.h>
#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stdbool.h>
#include <stdint.h>

#define STARBOOK_MTL_DMA_GUARD_REVISION 1U
#define STARBOOK_MTL_DMA_GUARD_ENGINES 2U
#define STARBOOK_MTL_DMA_GUARD_ARENAS 3U

enum starbook_mtl_dma_guard_engine_mode {
	STARBOOK_MTL_DMA_GUARD_DEFAULT_DENY_TRANSLATION = 1,
	STARBOOK_MTL_DMA_GUARD_BME_QUIESCED = 2,
};

struct starbook_mtl_dma_guard_engine {
	uint64_t base;
	uint64_t root;
	uint64_t capability;
	uint64_t extended_capability;
	uint32_t version;
	uint32_t status;
	uint32_t protected_memory_enable;
	uint32_t mode;
} __aligned(8);

struct starbook_mtl_dma_guard_range {
	uint64_t base;
	uint64_t size;
} __aligned(8);

struct starbook_mtl_dma_guard_snapshot {
	uint32_t revision;
	uint32_t size;
	uint64_t generation;
	uint8_t identity[32];
	uint32_t engine_count;
	uint32_t arena_count;
	struct starbook_mtl_dma_guard_engine engines[
		STARBOOK_MTL_DMA_GUARD_ENGINES];
	struct starbook_mtl_dma_guard_range handoff;
	struct starbook_mtl_dma_guard_range table;
	struct starbook_mtl_dma_guard_range table_mirror;
	struct starbook_mtl_dma_guard_range arenas[
		STARBOOK_MTL_DMA_GUARD_ARENAS];
	uint8_t reserved[16];
} __aligned(8);

/* Board-private, plain-value boundary between hardware reads and policy. */
struct starbook_mtl_dma_guard_facts {
	struct starbook_mtl_dma_guard_range live_buffer;
	struct starbook_mtl_dma_guard_range current_fsp_buffer;
	struct starbook_mtl_dma_guard_range table_mirror;
	struct starbook_mtl_dma_guard_range current_cbmem_mirror;
	struct starbook_mtl_dma_guard_engine engines[
		STARBOOK_MTL_DMA_GUARD_ENGINES];
	struct starbook_mtl_dma_guard_range handoff;
	struct starbook_mtl_dma_guard_range table;
	struct starbook_mtl_dma_guard_range arenas[
		STARBOOK_MTL_DMA_GUARD_ARENAS];
	uint64_t gfxvtbar;
	bool tables_match;
	bool integrated_requesters_verified;
} __aligned(8);

struct starbook_mtl_dma_guard_ops {
	void *context;
	enum cb_err (*ensure)(void *context);
	enum cb_err (*observe)(void *context,
		struct starbook_mtl_dma_guard_snapshot *snapshot);
	enum cb_err (*random64)(void *context, uint64_t *value);
	void (*poison)(void *context);
};

_Static_assert(sizeof(struct starbook_mtl_dma_guard_engine) == 48,
	"MTL DMA guard engine ABI changed");
_Static_assert(sizeof(struct starbook_mtl_dma_guard_snapshot) == 264,
	"MTL DMA guard snapshot ABI changed");
_Static_assert(sizeof(struct starbook_mtl_dma_guard_facts) == 256,
	"MTL DMA guard facts boundary changed");
_Static_assert(_Alignof(struct starbook_mtl_dma_guard_snapshot) == 8,
	"MTL DMA guard snapshot alignment changed");
_Static_assert(offsetof(struct starbook_mtl_dma_guard_snapshot, generation) == 8 &&
	offsetof(struct starbook_mtl_dma_guard_snapshot, engines) == 56 &&
	offsetof(struct starbook_mtl_dma_guard_snapshot, handoff) == 152,
	"MTL DMA guard snapshot fields moved");

enum cb_err starbook_mtl_dma_guard_capture(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_dma_guard_snapshot *snapshot);
enum cb_err starbook_mtl_dma_guard_policy_validate(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct starbook_mtl_dma_guard_snapshot *snapshot);
enum cb_err starbook_mtl_dma_guard_snapshot_build(
	const struct starbook_mtl_dma_guard_facts *facts,
	struct starbook_mtl_dma_guard_snapshot *snapshot);
enum cb_err starbook_mtl_dma_guard_capture_with_ops(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_dma_guard_snapshot *snapshot,
	const struct starbook_mtl_dma_guard_ops *ops);

#endif
