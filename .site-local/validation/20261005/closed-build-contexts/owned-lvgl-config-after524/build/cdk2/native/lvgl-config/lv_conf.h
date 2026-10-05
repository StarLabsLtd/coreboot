/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2024 Yang Gang; see LICENSE in this directory. */

#ifndef LV_CONF_H
#define LV_CONF_H

/*
 * CDK2 overrides for pinned LVGL 85aa60d18b3d5e5588d7b247abf90198f07c8a63.
 * Other settings use that revision's lv_conf_internal.h defaults.
 * The provenance test compares every effective LV_ macro with the original
 * configuration from commit 3616c6e4420aaeedee6eedab9ce50d0524b20a90.
 */
#define LV_COLOR_DEPTH 32
#define LV_DEF_REFR_PERIOD 10

/* Keep the existing built-in allocator and its exact memory budget. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (64 * 1024U)
#define LV_USE_UEFI 0

/* The renderer uses only the existing 32-bit and RGB888 software formats. */
#define LV_DRAW_SW_SUPPORT_A8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_I1 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_RGB565 0
#define LV_DRAW_SW_SUPPORT_RGB565A8 0
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0

/* Preserve the original font declarations and enabled sizes. */
#define LV_FONT_MANROPE_16 1
#define LV_FONT_MANROPE_20 1
#define LV_FONT_MANROPE_24 1
#define LV_FONT_MANROPE_30 1
#define LV_FONT_MANROPE_32 1
#define LV_FONT_MANROPE_40 1

#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_40 1

#define LV_FONT_CUSTOM_DECLARE \
	LV_FONT_DECLARE(lv_font_manrope_16) \
	LV_FONT_DECLARE(lv_font_manrope_20) \
	LV_FONT_DECLARE(lv_font_manrope_24) \
	LV_FONT_DECLARE(lv_font_manrope_30) \
	LV_FONT_DECLARE(lv_font_manrope_32) \
	LV_FONT_DECLARE(lv_font_manrope_40)

#define LV_USE_DEMO_KEYPAD_AND_ENCODER 1
#define LV_USE_PRIVATE_API 1

#endif
