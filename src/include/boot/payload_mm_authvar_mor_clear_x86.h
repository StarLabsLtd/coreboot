/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_H

#include <boot/payload_mm_authvar_mor_clear_executor.h>
#include <cpu/x86/pae.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_REVISION 1U

struct payload_mm_authvar_mor_clear_x86_arch_ops {
	enum cb_err (*page_tables_init)(void *page_tables);
	void (*map_2m)(void *page_tables, uint64_t physical, void *aperture);
	void (*paging_disable)(void);
	bool (*paging_active)(void);
	bool (*clflush_available)(void);
	void (*clflush_range)(uintptr_t base, size_t size);
	void (*memory_fence)(void);
};

/* Caller-owned state. It and both backing ranges must be plan exclusions. */
struct payload_mm_authvar_mor_clear_x86_backend {
	uint32_t revision;
	uint32_t size;
	uintptr_t page_tables;
	uintptr_t aperture;
	uint64_t mapped_physical;
	uintptr_t mapping;
	size_t mapped_size;
	struct payload_mm_authvar_mor_clear_x86_arch_ops arch;
	bool prepared;
	bool mapped;
	bool poisoned;
	uint8_t reserved[5];
};

enum cb_err payload_mm_authvar_mor_clear_x86_prepare(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops);

#if ENV_TEST
bool payload_mm_authvar_mor_clear_x86_paging_active_test(
	uintptr_t cr0, uintptr_t cr4);

enum cb_err payload_mm_authvar_mor_clear_x86_prepare_with_ops(
	const struct payload_mm_authvar_mor_clear_plan *plan,
	void *page_tables, void *aperture,
	struct payload_mm_authvar_mor_clear_x86_backend *backend,
	struct payload_mm_authvar_mor_clear_executor_ops *ops,
	const struct payload_mm_authvar_mor_clear_x86_arch_ops *arch);
#endif

#endif /* BOOT_PAYLOAD_MM_AUTHVAR_MOR_CLEAR_X86_H */
