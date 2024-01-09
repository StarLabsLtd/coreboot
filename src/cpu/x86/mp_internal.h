/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MP_COMMON_H
#define MP_COMMON_H

int mp_internal_get_num_cpus(void);

void mp_internal_ap_ready_for_instruction(int cur_cpu);
void mp_internal_ap_check_for_instruction(int cur_cpu);

#endif
