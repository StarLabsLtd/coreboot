/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef NORTHBRIDGE_INTEL_HASWELL_RAMINIT_H
#define NORTHBRIDGE_INTEL_HASWELL_RAMINIT_H

#include <types.h>

#define SPD_MEMORY_DOWN	0xff

struct spd_info {
	uint8_t addresses[4];
	unsigned int spd_index;
};

/* Mainboard callback to fill in the SPD addresses */
void mb_get_spd_map(struct spd_info *spdi);

void perform_raminit(const int s3resume);

#endif	/* NORTHBRIDGE_INTEL_HASWELL_RAMINIT_H */
