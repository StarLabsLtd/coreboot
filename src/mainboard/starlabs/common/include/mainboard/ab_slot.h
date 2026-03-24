/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef STARLABS_AB_SLOT_H
#define STARLABS_AB_SLOT_H

#define STARLABS_AB_SLOT_A                0
#define STARLABS_AB_SLOT_B                1

#define STARLABS_AB_CMOS_ACTIVE           49
#define STARLABS_AB_CMOS_PENDING          50
#define STARLABS_AB_CMOS_TRIES            51
#define STARLABS_AB_CMOS_CURRENT          52

#define STARLABS_AB_PENDING_VALID         0x80
#define STARLABS_AB_PENDING_SLOT_MASK     0x01
#define STARLABS_AB_CURRENT_VALID         0x80
#define STARLABS_AB_CURRENT_SLOT_MASK     0x01
#define STARLABS_AB_DEFAULT_BOOT_TRIES    3

void starlabs_ab_bootblock_init(void);

#endif
