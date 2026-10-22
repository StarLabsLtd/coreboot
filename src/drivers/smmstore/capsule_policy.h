/* SPDX-License-Identifier: LicenseRef-LICENSE-TBC */
#ifndef SMM_CAPSULE_POLICY_H
#define SMM_CAPSULE_POLICY_H

#include <boot/capsule_broker.h>

struct smm_capsule_image;

enum capsule_broker_authentication_status smm_capsule_policy(
	struct smm_capsule_image *image, const uint8_t image_type[16],
	uint32_t current_version, uint32_t lowest_version, uint32_t attempted_version,
	const void *fmap, size_t fmap_size, struct capsule_broker_raw_image *raw);

#endif
