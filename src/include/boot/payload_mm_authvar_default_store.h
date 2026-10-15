/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_H

#include <stddef.h>

enum payload_mm_authvar_default_store_source {
	PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_INVALID = 0,
	PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_FOREIGN,
	PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_ERASED,
	PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_NOR_SUBSET,
	PAYLOAD_MM_AUTHVAR_DEFAULT_STORE_COMPLETE,
};

/*
 * Compose the complete EDK2 26.09 SetupMode default image without accessing
 * media.  COMPLETE means byte equality with that full three-record image; a
 * formatted empty store is a NOR_SUBSET, not COMPLETE.  FOREIGN still yields
 * the deterministic candidate, but the caller must not consume it.  Invalid
 * arguments leave candidate byte-exact untouched.
 *
 * source and candidate each cover the complete SMMSTORE region and must be
 * disjoint.  A later same-lease executor is responsible for all media work.
 */
enum payload_mm_authvar_default_store_source
payload_mm_authvar_default_store_compose(const void *source, void *candidate,
					 size_t region_size, size_t block_size);

#endif
