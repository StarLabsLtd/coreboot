/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_INSTALL_H

#include <boot/payload_mm_authvar_presence_lifecycle_close_route.h>

#if ENV_SMM || ENV_TEST
/*
 * A later MTL policy owner must supply this protected contract.  In
 * particular, installation must not infer DMA protection or reset/S3
 * invocation ownership from the presence route.
 */
struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies {
	const struct payload_mm_authvar_presence_lifecycle_close_internal_policy
		*internal;
	/* Retained from provider_provision before the installation SMI is armed. */
	const struct smm_invocation_save_state_ops *active_ops;
	payload_mm_authvar_protected_storage protected_storage;
	void *protected_storage_context;
	/*
	 * Proves the complete nonwrapping frame is mapped ordinary shared RAM,
	 * disjoint from SMRAM, save-state allocations and protected objects.
	 * The receiver checks it before the first read and before every writeback.
	 */
	payload_mm_authvar_presence_transaction_range_fn communication_range_valid;
	void *communication_range_context;
	size_t communication_range_context_size;
};

enum cb_err starbook_mtl_authvar_presence_lifecycle_close_install_receive(
	const struct starbook_mtl_authvar_presence_lifecycle_close_install_dependencies
		*dependencies);
#endif

#endif
