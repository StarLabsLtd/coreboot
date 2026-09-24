/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR_H

#include <boot/payload_mm_authvar_mor_clear.h>

struct payload_mm_authvar_mor_clear_executor_ops {
	void *context;
	size_t window_bytes;
	enum cb_err (*dma_snapshot)(void *context,
		struct payload_mm_authvar_mor_clear_dma_snapshot *snapshot);
	enum cb_err (*map_window)(void *context, uint64_t physical,
		size_t size, void **mapping);
	enum cb_err (*cache_writeback_invalidate)(void *context, uint64_t physical,
		const volatile void *mapping, size_t size);
	enum cb_err (*fence)(void *context);
	enum cb_err (*unmap_window)(void *context, uint64_t physical,
		void *mapping, size_t size);
};

enum cb_err payload_mm_authvar_mor_clear_execute(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	uint64_t cold_boot_generation,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant);

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR_H */
