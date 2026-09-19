/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_FMP_AUTH_POLICY_H
#define PAYLOAD_MM_FMP_AUTH_POLICY_H

#include <boot/payload_mm_authvar.h>
#include <payload_mm_cms.h>

#define PAYLOAD_MM_FMP_AUTH_POLICY_REVISION 1U
#define PAYLOAD_MM_FMP_BOARD_IDENTITY_SIZE 64U
#define PAYLOAD_MM_FMP_MAX_ROM_SIZE (64U * 1024U * 1024U)

struct payload_mm_fmp_owner_record;

struct payload_mm_fmp_auth_policy {
	uint32_t revision;
	uint32_t size;
	guid_t image_type;
	uint32_t trusted_lowest_version;
	uint32_t image_size;
	const void *trust_xdr;
	size_t trust_xdr_size;
	const char *mainboard_vendor;
	size_t mainboard_vendor_size;
	const char *mainboard_part;
	size_t mainboard_part_size;
};

/*
 * Install once from trusted coreboot-owned facts before broker use. The
 * complete trust and identity policy is copied into protected storage. The
 * provider is synchronous, accepts no context and retains no image pointer.
 */
enum cb_err payload_mm_fmp_auth_policy_install(
	const struct payload_mm_fmp_auth_policy *trusted_policy,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
enum cb_err payload_mm_fmp_authenticate_provider(const void *context,
	const void *image, size_t image_size, uint32_t attempted_version,
	const struct payload_mm_fmp_owner_record *owner_record);

#if ENV_TEST
const void *payload_mm_fmp_auth_policy_test_authority(size_t *size);
#endif

#endif
