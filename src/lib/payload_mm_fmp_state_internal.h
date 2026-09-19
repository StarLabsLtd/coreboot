/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_STATE_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_STATE_INTERNAL_H

#include <boot/payload_mm_authvar.h>

struct payload_mm_fmp_state_identity {
	guid_t namespace_guid;
	uint64_t hardware_instance;
	uint32_t trusted_lowest_version;
	uint32_t variable_name_bytes;
	uint16_t variable_name[PAYLOAD_MM_FMP_STATE_NAME_CAPACITY];
};

bool payload_mm_fmp_state_authority_ready(void);
enum cb_err payload_mm_fmp_state_identity_get(
	struct payload_mm_fmp_state_identity *identity);
enum cb_err payload_mm_fmp_state_checkpoint_build(
	const uint8_t current[PAYLOAD_MM_FMP_STATE_WIRE_SIZE],
	uint32_t attempted_version, struct payload_mm_fmp_state_identity *identity,
	uint8_t candidate[PAYLOAD_MM_FMP_STATE_WIRE_SIZE]);

#endif
