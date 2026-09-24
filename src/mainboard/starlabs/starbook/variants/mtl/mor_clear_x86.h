/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_H

#include "dma_guard.h"

#include <boot/payload_mm_authvar_mor_clear_x86.h>
#include <bootmem.h>

#define STARBOOK_MTL_MOR_CLEAR_X86_REVISION 1U

struct starbook_mtl_mor_clear_x86_reservations {
	uint32_t revision;
	uint32_t size;
	struct bootmem_aligned_reservation_handle handles[2];
	uint32_t registered;
	uint32_t reserved;
};

struct starbook_mtl_mor_clear_x86_binding {
	struct payload_mm_authvar_mor_clear_x86_backend backend;
	struct payload_mm_authvar_mor_clear_executor_ops ops;
};

_Static_assert(sizeof(struct starbook_mtl_mor_clear_x86_reservations) == 32,
	"MTL MOR x86 reservation state changed");

enum cb_err starbook_mtl_mor_clear_x86_register(
	struct starbook_mtl_mor_clear_x86_reservations *reservations);
enum cb_err starbook_mtl_mor_clear_x86_prepare(
	const struct starbook_mtl_mor_clear_x86_reservations *reservations,
	const struct starbook_mtl_dma_guard_snapshot *dma_guard,
	bool resume_from_s3, struct payload_mm_authvar_mor_clear_plan *plan,
	struct starbook_mtl_mor_clear_x86_binding *binding);

#endif /* MAINBOARD_STARLABS_STARBOOK_MTL_MOR_CLEAR_X86_H */
