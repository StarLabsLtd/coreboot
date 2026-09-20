/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_CAPSULE_BROKER_INFO_INTERNAL_H
#define LIB_CAPSULE_BROKER_INFO_INTERNAL_H

#include <boot/capsule_broker.h>

struct capsule_broker_info_snapshot {
	guid_t image_type;
	uint64_t hardware_instance;
	uint32_t current_version;
	uint32_t lowest_supported_version;
	uint32_t image_size;
	uint32_t capabilities;
	uint32_t state_flags;
	uint32_t last_attempt_version;
	uint32_t last_attempt_status;
};

enum cb_err capsule_broker_info_read(
	struct capsule_broker_info_snapshot *snapshot);

#if ENV_TEST
const void *capsule_broker_info_test_authority(size_t *size);
#endif

#endif
