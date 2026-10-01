/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_H
#define BOOT_PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_REVISION 1U
#define PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE 96U
#define PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_KEYS 4U
/* Explicit selected factory-limit profile, not a universal producer limit. */
#define PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_MAX_VARIABLE_SIZE (1024U * 1024U)
#define PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_MAX_SIZE \
	(PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_HEADER_SIZE + \
	 PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_KEYS * \
	 PAYLOAD_MM_IMAGE_POLICY_SNAPSHOT_MAX_VARIABLE_SIZE)

/* Layout only: neither producer/delivery authority nor image permission. */
bool payload_mm_image_policy_snapshot_shape_valid(const void *data, size_t size);

#endif
