/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_H

#include "dma_guard.h"

#include <boot/payload_mm_authvar_mor_clear_x86.h>
#include <boot/payload_mm_authvar_mor_live_inventory.h>
#include <bootmem.h>

#define STARBOOK_MTL_MOR_CLEAR_X86_REVISION 1U
#define STARBOOK_MTL_MOR_CLEAR_X86_OVERLAYS 4U

struct starbook_mtl_mor_clear_x86_reservations {
	uint32_t revision;
	uint32_t size;
	struct bootmem_aligned_reservation_handle handles[2];
	uint32_t registered;
	uint32_t reserved;
};

struct starbook_mtl_mor_clear_x86_authority {
	uint32_t revision;
	uint32_t size;
	uintptr_t owner;
	uintptr_t plan_address;
	uintptr_t page_tables;
	uintptr_t aperture;
	struct starbook_mtl_dma_guard_snapshot prepared;
	struct starbook_mtl_dma_guard_snapshot bound;
	struct payload_mm_authvar_mor_clear_dma_snapshot dma;
	struct payload_mm_authvar_mor_clear_plan plan;
	struct payload_mm_authvar_mor_live_inventory_overlay overlays[
		STARBOOK_MTL_MOR_CLEAR_X86_OVERLAYS];
	struct payload_mm_authvar_mor_clear_x86_backend backend;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
	uint64_t seal;
};

struct starbook_mtl_mor_clear_x86_binding {
	struct payload_mm_authvar_mor_clear_x86_backend backend;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
	struct starbook_mtl_mor_clear_x86_authority authority;
	struct starbook_mtl_mor_clear_x86_authority authority_mirror;
	uint32_t phase;
	uint32_t reserved;
};

_Static_assert(sizeof(struct starbook_mtl_mor_clear_x86_reservations) == 32,
	"MTL MOR x86 reservation state changed");
_Static_assert(offsetof(struct starbook_mtl_mor_clear_x86_binding, backend) == 0,
	"MTL MOR x86 context must be the binding base");
_Static_assert(offsetof(struct starbook_mtl_mor_clear_x86_authority, seal) +
	sizeof(uint64_t) == sizeof(struct starbook_mtl_mor_clear_x86_authority),
	"MTL MOR x86 authority seal must cover every preceding byte");

enum cb_err starbook_mtl_mor_clear_x86_register(
	struct starbook_mtl_mor_clear_x86_reservations *reservations);
enum cb_err starbook_mtl_mor_clear_x86_prepare(
	const struct starbook_mtl_mor_clear_x86_reservations *reservations,
	const struct starbook_mtl_dma_guard_snapshot *dma_guard,
	bool resume_from_s3, struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_clear_x86_binding *binding);

#if ENV_TEST
void starbook_mtl_mor_clear_x86_lifecycle_reset_test(void);
#endif

#endif /* MAINBOARD_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_H */
