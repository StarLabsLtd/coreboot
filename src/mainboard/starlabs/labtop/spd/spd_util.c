/* SPDX-License-Identifier: GPL-2.0-only */

#include <stdint.h>
#include <string.h>

#include "spd.h"

void mainboard_fill_rcomp_res_data(void *rcomp_ptr)
{
	/* Rcomp resistor */
	const u16 RcompResistor[3] = {121, 81, 100};
	memcpy(rcomp_ptr, RcompResistor, sizeof(RcompResistor));
}

void mainboard_fill_rcomp_strength_data(void *rcomp_strength_ptr)
{
	/* Rcomp target */
	static const u16 RcompTarget[5] = {100, 40, 20, 20, 26};

	memcpy(rcomp_strength_ptr, RcompTarget, sizeof(RcompTarget));
}
