/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_QEMU_PFLASH_H
#define BOOT_PAYLOAD_MM_AUTHVAR_QEMU_PFLASH_H

#include <commonlib/bsd/cb_err.h>

/* Private SMM initialization prerequisite; this publishes no service. */
enum cb_err payload_mm_authvar_qemu_pflash_install(void);

#endif
