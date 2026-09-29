/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_PRE_LOCK_DISPATCH_H
#define CPU_X86_SMM_PRE_LOCK_DISPATCH_H

#include <stdint.h>

enum smm_pre_lock_dispatch_result {
	SMM_PRE_LOCK_DISPATCH_NOT_HANDLED,
	SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED,
	SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED,
};

#if ENV_SMM || ENV_TEST
enum smm_pre_lock_dispatch_result smm_pre_lock_dispatch(
	uint32_t cpu, uint32_t initial_apic_id);
#endif

#endif
