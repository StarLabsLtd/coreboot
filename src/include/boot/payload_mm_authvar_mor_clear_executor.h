/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR_H

#include <boot/payload_mm_authvar_mor_clear.h>

#define PAYLOAD_MM_AUTHVAR_MOR_CLEAR_WORKSPACE_REVISION 1U
#define PAYLOAD_MM_AUTHVAR_MOR_CLEAR_WORKSPACE_BYTES 2400U

struct payload_mm_authvar_mor_clear_workspace {
	uint32_t revision;
	uint32_t size;
	uint8_t lifecycle;
	uint8_t reserved[7];
	uint8_t storage[PAYLOAD_MM_AUTHVAR_MOR_CLEAR_WORKSPACE_BYTES];
} __aligned(8);

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
	/* Appended while this contract is dormant; preserve the existing prefix. */
	void *inventory_context;
	enum cb_err (*inventory_validate)(void *context,
		const struct payload_mm_authvar_mor_clear_plan *plan);
	size_t context_size;
	size_t inventory_context_size;
	const void *executable_owner;
	size_t executable_owner_size;
	const void *stack_owner;
	size_t stack_owner_size;
};

enum cb_err payload_mm_authvar_mor_clear_execute(
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	const struct payload_mm_authvar_mor_clear_plan *plan,
	const struct payload_mm_authvar_mor_entry *entry,
	uint64_t cold_boot_generation,
	const struct payload_mm_authvar_mor_clear_executor_ops *ops,
	struct payload_mm_authvar_mor_clear_transcript *transcript,
	struct payload_mm_authvar_mor_grant *grant);

#if ENV_TEST
void payload_mm_authvar_mor_clear_executor_mapping_tamper_test(
	struct payload_mm_authvar_mor_clear_workspace *workspace,
	uint64_t physical, void *mapping, size_t size);
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_EXECUTOR_H */
