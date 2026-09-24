/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_AUTHVAR_RECOVERY_H
#define PAYLOAD_MM_AUTHVAR_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if ENV_TEST && CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
enum payload_mm_authvar_recovery_test_mutation {
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_NONE,
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_SNAPSHOT,
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_GEOMETRY,
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_STEP,
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_TOKEN,
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_CALLBACK_IMAGE,
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_ABORT_READ_IMAGE,
	PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_REPLAY_READ_IMAGE,
};

void payload_mm_authvar_recovery_test_arm_image_mutation(void *image,
	size_t size, uint32_t read_offset, size_t read_size,
	unsigned int matches_to_skip);

uint64_t payload_mm_authvar_executor_test_recovery_plan(
	enum payload_mm_authvar_recovery_test_mutation mutation, bool execute);
#endif

#endif
