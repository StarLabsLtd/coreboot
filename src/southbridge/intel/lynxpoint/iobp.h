/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOUTHBRIDGE_INTEL_LYNXPOINT_IOBP_H
#define SOUTHBRIDGE_INTEL_LYNXPOINT_IOBP_H

#include <stdint.h>

u32 pch_iobp_read(u32 address);
void pch_iobp_write(u32 address, u32 data);
void pch_iobp_update(u32 address, u32 andvalue, u32 orvalue);
void pch_iobp_exec(u32 addr, u16 op_dcode, u8 route_id, u32 *data, u8 *resp);

#endif
