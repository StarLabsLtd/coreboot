/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_FMP_BOOT_H
#define BOOT_PAYLOAD_MM_FMP_BOOT_H

#include <boot/payload_mm_authvar.h>

/* Reconcile the real protected store and install its boot-local FMP owner.
 * The caller must first install the trusted coreboot state policy. This does
 * not publish a capsule endpoint or establish hardware rollback protection. */
enum cb_err payload_mm_fmp_owner_authvar_boot_install(
	payload_mm_authvar_protected_storage storage_is_protected, void *context);

#endif
