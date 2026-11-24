/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef AMD_PEI_GOP_INIT_H
#define AMD_PEI_GOP_INIT_H
#include <commonlib/bsd/compiler.h>
#include <stdint.h>
/* Documented IA32 graphics policy ABI; no native pointer-sized fields. */
struct gop_init_policy {
	uint16_t length;
	uint8_t format, content;
	uint32_t bars[6], vbios;
	uint8_t lid_closed;
	uint32_t width, height;
} __packed;
struct gop_mode {
	uint64_t framebuffer, framebuffer_size;
	uint32_t width, height, scanline, pixel_format;
};
_Static_assert(sizeof(struct gop_init_policy) == 41, "IA32 policy ABI");
_Static_assert(sizeof(struct gop_mode) == 32, "Mode output ABI");
#endif
