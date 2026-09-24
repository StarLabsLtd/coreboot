/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef BOOT_PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_H
#define BOOT_PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_H

#include <boot/payload_mm_authvar_policy.h>

enum payload_mm_authvar_controlled_mode {
	PAYLOAD_MM_AUTHVAR_CONTROLLED_MODE_NONE = 0,
	PAYLOAD_MM_AUTHVAR_CONTROLLED_SECURE_BOOT_ENABLE,
	PAYLOAD_MM_AUTHVAR_CONTROLLED_CUSTOM_MODE,
};

enum payload_mm_authvar_controlled_mode
payload_mm_authvar_controlled_mode_classify(const uint8_t vendor_guid[16],
	const void *name, size_t name_size);

/* EDK2 VarCheck property hook: call before store lookup and existing checks. */
uint64_t payload_mm_authvar_controlled_mode_property(
	enum payload_mm_authvar_controlled_mode mode, uint32_t attributes,
	size_t payload_size);

/* EDK2 AuthVariable physical-presence hook: call after existing checks. */
uint64_t payload_mm_authvar_controlled_mode_authorize(
	enum payload_mm_authvar_controlled_mode mode,
	bool trusted_physical_presence);

#endif
