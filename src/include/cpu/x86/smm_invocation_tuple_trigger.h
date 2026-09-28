/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_TUPLE_TRIGGER_H
#define CPU_X86_SMM_INVOCATION_TUPLE_TRIGGER_H

#include <stdint.h>

/* Return the raw post-SMI logical value transported in ECX:EAX. */
uint64_t smm_invocation_tuple_trigger(void);

#endif
