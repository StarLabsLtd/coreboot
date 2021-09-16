/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * EC communication interface for ITE ITE Embedded Controller.
 */

#ifndef _EC_STARLABS_ITE_CFG_H
#define _EC_STARLABS_ITE_CFG_H

/* IT5570 chip ID byte values. */
#define ITE_CHIPID1_VAL		0x89
#define ITE_CHIPID2_VAL		0x87

/* EC RAM offsets. */
#define ECRAM_KBL_BRIGHTNESS	0x19
#define ECRAM_KBL_STATE		0x18
#define ECRAM_TRACKPAD_STATE	0x14
#define ECRAM_FN_LOCK_STATE	0x2c
#define ECRAM_KBL_TIMEOUT	0x1a
#define ECRAM_FN_CTRL_REVERSE	0x43
#define ECRAM_MAX_CHARGE	0xff /* TODO: Add */
#define ECRAM_FAN_MODE		0x42

#endif
