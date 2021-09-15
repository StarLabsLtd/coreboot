/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * EC communication interface for ITE ITE Embedded Controller.
 */

#ifndef _EC_STARLABS_ITE_CFG_H
#define _EC_STARLABS_ITE_CFG_H

/* IT5570 chip ID byte values. */
#define ITE_CHIPID1_VAL		0x55
#define ITE_CHIPID2_VAL		0x70

/* EC RAM offsets. */
#define ECRAM_KBL_BRIGHTNESS	0x09
#define ECRAM_KBL_STATE		0x0a
#define ECRAM_TRACKPAD_STATE	0x0c
#define ECRAM_FN_LOCK_STATE	0x0f
#define ECRAM_KBL_TIMEOUT	0x10
#define ECRAM_FN_CTRL_REVERSE	0x17
#define ECRAM_MAX_CHARGE	0x1a
#define ECRAM_FAN_MODE		0x1b

#endif
