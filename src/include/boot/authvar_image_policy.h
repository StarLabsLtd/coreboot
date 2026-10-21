/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_AUTHVAR_IMAGE_POLICY_H
#define BOOT_AUTHVAR_IMAGE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUTHVAR_IMAGE_POLICY_SNAPSHOT_REVISION 1U
#define AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE 96U
/* Maximum admitted variable size in this wire format. */
#define AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_VARIABLE_SIZE (1024U * 1024U)
#define AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_SIZE \
	(AUTHVAR_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE + \
	 4U * AUTHVAR_IMAGE_POLICY_SNAPSHOT_MAX_VARIABLE_SIZE)

enum authvar_image_policy_snapshot_key {
	/* PK/global namespace; db/dbx/dbt/image-security namespace, fixed order. */
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_PK,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_DB,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_DBX,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_DBT,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_KEYS,
};

enum authvar_image_policy_snapshot_mode {
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_SETUP = 1U,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_SECURE_BOOT = 2U,
	AUTHVAR_IMAGE_POLICY_SNAPSHOT_VENDOR_KEYS = 4U,
};

struct authvar_image_policy_snapshot_blob {
	const uint8_t *data;
	uint32_t size;
	uint32_t attributes;
};

struct authvar_image_policy_snapshot_view {
	uint32_t modes;
	struct authvar_image_policy_snapshot_blob blobs[AUTHVAR_IMAGE_POLICY_SNAPSHOT_KEYS];
};

/*
 * Shape/profile decoding only: no authoritative producer, DMA delivery lease,
 * certificate trust or permission to execute is established. Inputs must stay
 * immutable for the call and the lifetime of successful borrowed blob views.
 * Validate every descriptor/padding byte before publishing any output. Failure
 * leaves output unchanged; output must be aligned, nonwrapping and disjoint.
 * Known stored attributes are preserved, not authenticated-key requirements.
 * NV+TIME/BS admission belongs to the separate policy consumer. This does not
 * validate individual ESL/certificate data or the actual protected index rules.
 */
bool authvar_image_policy_snapshot_decode_shape(const void *data, size_t size,
	struct authvar_image_policy_snapshot_view *output);

#endif
