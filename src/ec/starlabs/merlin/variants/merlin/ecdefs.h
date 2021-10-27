/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * EC communication interface for ITE Embedded Controller
 */

#ifndef _EC_STARLABS_ITE_CFG_H
#define _EC_STARLABS_ITE_CFG_H

/* IT5570 chip ID byte values */
#define ITE_CHIPID1_VAL		0x55
#define ITE_CHIPID2_VAL		0x70

/* EC RAM offsets */
#define ECRAM_FN_CTRL_REVERSE	0x30
#define ECRAM_FN_LOCK_STATE	0x31
#define ECRAM_TRACKPAD_STATE	0x32
#define ECRAM_KBL_BRIGHTNESS	0x33
#define ECRAM_KBL_STATE		0x34
#define ECRAM_KBL_TIMEOUT	0x35
#define ECRAM_FAN_MODE		0x50
#define ECRAM_MAX_CHARGE	0x51

#endif
