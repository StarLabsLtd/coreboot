/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * EC communication interface for ITE IT8987 Embedded Controller.
 */

#ifndef _EC_STARLABS_IT8987_H
#define _EC_STARLABS_IT8987_H

/*
 * Define the expected value of the PNP base address that is fixed through
 * the BADRSEL register controlled within the EC domain by the binary blob.
 */
#define IT8987E_FIXED_ADDR	0x4e

/* Logical device number (LDN) assignments. */
#define IT8987E_SP1	0x01	/* Com1 */
#define IT8987E_SP2	0x02	/* Com2 */
#define IT8987E_SWUC	0x04	/* System Wake-Up */
#define IT8987E_KBCM	0x05	/* PS/2 mouse */
#define IT8987E_KBCK	0x06	/* PS/2 keyboard */
#define IT8987E_IR	0x0a	/* Consumer IR */
#define IT8987E_SMFI	0x0f	/* Shared Memory/Flash Interface */
#define IT8987E_RTCT	0x10	/* RTC-like Timer */
#define IT8987E_PMC1	0x11	/* Power Management Channel 1 */
#define IT8987E_PMC2	0x12	/* Power Management Channel 2 */
#define IT8987E_SSPI	0x13	/* Serial Peripheral Interface */
#define IT8987E_PECI	0x14	/* Platform EC Interface */
#define IT8987E_PMC3	0x17	/* Power Management Channel 3 */
#define IT8987E_PMC4	0x18	/* Power Management Channel 4 */
#define IT8987E_PMC5	0x19	/* Power Management Channel 5 */

/* Host domain registers. */
#define IT8987_CHIPID1	0x20	/* Device ID register 1 */
#define IT8987_CHIPID2	0x21	/* Device ID register 2 */

/* IT8987 chip ID byte values. */
#define IT8987_CHIPID1_VAL 0x89
#define IT8987_CHIPID2_VAL 0x87

/* EC RAM offsets. */
#define ECRAM_KBL_BRIGHTNESS	0x16 /* CML */
#define ECRAM_KBL_STATE		0x15 /* CML */
#define ECRAM_TRACKPAD_STATE	0x11 /* CML */
#define ECRAM_FN_LOCK_STATE	0x2c /* CML */
#define ECRAM_KBL_TIMEOUT	0x07 /* CML */
#define ECRAM_FN_CTRL_REVERSE	0x08 /* CML */
#define ECRAM_MAX_CHARGE	0xff /* TBC */
#define ECRAM_FAN_MODE		0x08 /* CML */

/*
 * CMOS Settings
 */

/* Keyboard Backlight Timeout */
#define SEC_30			0x00
#define MIN_1			0x01
#define MIN_3			0x02
#define MIN_5			0x03
#define NEVER			0x04

/* Fn Ctrl Swap */
#define FN_CTRL			0x00
#define CTRL_FN			0x01

/* Max Charge Setting */
#define CHARGE_100		0x00
#define CHARGE_80		0x01
#define CHARGE_60		0x02

/* Fan Mode Setting */
#define FAN_NORMAL		0x00
#define FAN_AGGRESSIVE		0x01
#define FAN_QUIET		0xa2

/* Fn Lock State */
#define UNLOCKED		0x00
#define LOCKED			0x01

/* Trackpad State */
#define TRACKPAD_ENABLED	0x00
#define TRACKPAD_DISABLED	0x01

/* Keyboard Brightness Levels */
#define KBL_OFF			0x00
#define KBL_LOW			0x01
#define KBL_HIGH		0x02

/* Keyboard Backlight State */
#define KBL_DISABLE		0x00
#define KBL_ENABLE		0x01

/*
 * Accepted Values when they differ from CMOS
 */

/* Max Charge Values */
#define VAL_CHARGE_100		0x00
#define VAL_CHARGE_80		0xbb
#define VAL_CHARGE_60		0xaa

/* Fan Levels */
#define VAL_FAN_AGGRESSIVE	0xbb
#define VAL_FAN_NORMAL		0x00
#define VAL_FAN_QUIET		0xaa

/* Trackpad State */
#define VAL_TRACKPAD_ENABLED	0x00
#define VAL_TRACKPAD_DISABLED	0x22

/* Keyboard Brightness Levels */
#define VAL_KBL_OFF		0xcc
#define VAL_KBL_LOW		0xbb
#define VAL_KBL_HIGH		0xaa

/* Keyboard Backlight State */
#define VAL_KBL_DISABLED	0x00
#define VAL_KBL_ENABLED		0xaa

u16 it_get_version(void);

#endif
