/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SOUTHBRIDGE_INTEL_INSMM_STS_H
#define SOUTHBRIDGE_INTEL_INSMM_STS_H

#include <types.h>

#if CONFIG(HAVE_INSMM_STS)
void set_insmm_sts(bool enable_writes);
#else
static inline void set_insmm_sts(bool enable_writes) {}
#endif

#endif /* SOUTHBRIDGE_INTEL_INSMM_STS_H */
