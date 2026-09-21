/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_CAPSULE_PLATFORM_ADAPTER_INTERNAL_H
#define LIB_CAPSULE_PLATFORM_ADAPTER_INTERNAL_H

#include <boot/capsule_platform_facts.h>
#include <boot/payload_mm_authvar.h>
#include <payload_mm_fmp_auth_policy.h>

#include "capsule_update_internal.h"

struct capsule_platform_media_context {
	uint64_t media_size;
	uint64_t staging_base;
	uint64_t staging_size;
	uint32_t block_size;
	uint32_t erase_size;
};

struct capsule_platform_auth_identity {
	guid_t image_type;
	uint32_t trusted_lowest_version;
	uint32_t image_size;
	uint32_t mainboard_vendor_size;
	uint32_t mainboard_part_size;
	char mainboard_vendor[PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE];
	char mainboard_part[PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE];
};

struct capsule_platform_identity {
	struct payload_mm_fmp_state_policy state;
	struct capsule_broker_info_policy info;
	struct capsule_platform_auth_identity authentication;
};

/*
 * Build only a boot-device transport. This deliberately supplies no DMA,
 * SMM-only SPI, raw-flash exclusion or CPU-rendezvous proof.
 */
enum cb_err capsule_platform_media_adapter_build(
	const struct capsule_platform_facts *facts,
	struct capsule_platform_media_context *context,
	struct capsule_media_backend *backend);

/*
 * Exact geometric containment in the live SMM region; context must be NULL.
 * This is not by itself a hardware-protection proof and must not be passed to
 * an authority install until a platform has independently verified SMRAM.
 */
bool capsule_platform_smm_storage_contains(void *context,
	const void *storage, size_t size);

/* Derive owner, INFO and authentication identity from one facts snapshot. */
enum cb_err capsule_platform_identity_build(
	const struct capsule_platform_facts *facts,
	struct capsule_platform_identity *identity);

#endif
