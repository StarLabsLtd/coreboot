/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_MOR_PROBE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_MOR_PROBE_H

#include <commonlib/bsd/cb_err.h>
#include <commonlib/bsd/compiler.h>
#include <stddef.h>
#include <stdint.h>

/* Native, pointer-free result. Absence is represented by present == 0. */
struct payload_mm_authvar_mor_entry {
	uint8_t present;
	uint8_t value;
	uint16_t reserved;
} __aligned(4);

_Static_assert(sizeof(struct payload_mm_authvar_mor_entry) == 4,
	"MOR entry result layout changed");
_Static_assert(_Alignof(struct payload_mm_authvar_mor_entry) == 4,
	"MOR entry result alignment changed");
_Static_assert(offsetof(struct payload_mm_authvar_mor_entry, present) == 0 &&
	offsetof(struct payload_mm_authvar_mor_entry, value) == 1 &&
	offsetof(struct payload_mm_authvar_mor_entry, reserved) == 2,
	"MOR entry result fields moved");

enum cb_err payload_mm_authvar_mor_probe_entry(
	struct payload_mm_authvar_mor_entry *entry);

#endif
