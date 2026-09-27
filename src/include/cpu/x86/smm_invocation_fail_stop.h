/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef CPU_X86_SMM_INVOCATION_FAIL_STOP_H
#define CPU_X86_SMM_INVOCATION_FAIL_STOP_H

#include <commonlib/bsd/compiler.h>

/*
 * A selected platform must provide this SMM-linked terminal action. It must
 * attempt a platform-wide reset, use a platform-wide fallback reset or
 * watchdog if that fails, and terminally halt only as the final fallback. It
 * must never return through RSM or merely stop the calling CPU.
 */
void smm_invocation_platform_fail_stop(void) __noreturn;

#endif
