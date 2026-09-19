/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_CAPSULE_UPDATE_H
#define BOOT_CAPSULE_UPDATE_H

#include <commonlib/coreboot_tables.h>
#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define CAPSULE_UPDATE_MAX_REGIONS 16

struct capsule_update_plan {
	const uint8_t *image;
	size_t image_bytes;
	const struct lb_capsule_update_region *regions;
	size_t region_count;
};

/*
 * This is intentionally one bounded operation, not a raw flash transport.
 * A backend must preserve every byte outside plan->regions and verify every
 * written byte before returning success.
 */
struct capsule_update_backend {
	uint32_t capabilities;
	enum cb_err (*apply_regions)(const struct capsule_update_plan *plan);
};

enum cb_err capsule_handoff_validate(const struct lb_capsule_handoff *handoff,
	size_t bytes, const struct lb_efi_fw_info *firmware);

#endif
