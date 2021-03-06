/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _MAINBOARD_SPD_H_
#define _MAINBOARD_SPD_H_

#include <gpio.h>

void mainboard_fill_dq_map_data(void *dq_map_ptr);
void mainboard_fill_dqs_map_data(void *dqs_map_ptr);
void mainboard_fill_rcomp_res_data(void *rcomp_ptr);
void mainboard_fill_rcomp_strength_data(void *rcomp_strength_ptr);

#endif
