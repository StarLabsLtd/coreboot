/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * EC communication interface for ITE IT8987 Embedded Controller.
 */

#ifndef _EC_STARLABS_IT8987_H
#define _EC_STARLABS_IT8987_H

/* Command / data ports. */
#define IT8987E_ADDR  0x4e
#define IT8987E_DATA  0x4f

/* Logical device number (LDN) assignments. */
#define IT8987E_SP1  0x01 /* Com1 */
#define IT8987E_SP2  0x02 /* Com2 */
#define IT8987E_SWUC 0x04 /* System Wake-Up */
#define IT8987E_KBCM 0x05 /* PS/2 mouse */
#define IT8987E_KBCK 0x06 /* PS/2 keyboard */
#define IT8987E_IR   0x0a /* Consumer IR */
#define IT8987E_SMFI 0x0f /* Shared Memory/Flash Interface */
#define IT8987E_RTCT 0x10 /* RTC-like Timer */
#define IT8987E_PMC1 0x11 /* Power Management Channel 1 */
#define IT8987E_PMC2 0x12 /* Power Management Channel 2 */
#define IT8987E_SSPI 0x13 /* Serial Peripheral Interface */
#define IT8987E_PECI 0x14 /* Platform EC Interface */
#define IT8987E_PMC3 0x17 /* Power Management Channel 3 */
#define IT8987E_PMC4 0x18 /* Power Management Channel 4 */
#define IT8987E_PMC5 0x19 /* Power Management Channel 5 */

/* EC RAM offsets. */
#define	ECRAM_KBL_TIMEOUT	0x07
#define ECRAM_FN_CTRL_REVERSE	0x08
#define ECRAM_FN_LOCK_STATE	0x2C

void it8987_write_data(u8 addr, u8 data);
u8 it8987_read_data(u8 addr);
u16 it8987_read_chipid(void);
u16 it8987_get_version(void);

#endif
