/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <security/tpm/tspi.h>
#include <security/tpm/tss.h>
#include <security/vboot/tpm_common.h>
#include <security/tpm/tss_errors.h>
#include <vb2_api.h>
#include <vb2_sha.h>
#include <string.h>

#if CONFIG(PAYLOAD_CDK2) && CONFIG(TPM2) && CONFIG(TPM_LOG_TPM2)
#include <security/tpm/tspi/crtm.h>
#endif

#define TPM_PCR_BOOT_MODE "VBOOT: boot mode"
#define TPM_PCR_GBB_HWID_NAME "VBOOT: GBB HWID"
#define TPM_PCR_FIRMWARE_VERSION "VBOOT: firmware ver"

tpm_result_t vboot_setup_tpm(struct vb2_context *ctx)
{
	tpm_result_t rc;

	if (CONFIG(VBOOT_MOCK_SECDATA) && tpm_is_expected_absent()) {
		printk(BIOS_DEBUG, "VBOOT: TPM absent by platform policy; using mock secdata\n");
		return TPM_SUCCESS;
	}

	rc = tpm_setup(ctx->flags & VB2_CONTEXT_S3_RESUME);
	if (rc == TPM_CB_MUST_REBOOT)
		ctx->flags |= VB2_CONTEXT_SECDATA_WANTS_REBOOT;

	return rc;
}

tpm_result_t vboot_extend_pcr(struct vb2_context *ctx, int pcr,
			     enum vb2_pcr_digest which_digest)
{
	uint8_t buffer[TPM_PCR_MAX_LEN];
	uint32_t size = sizeof(buffer);
	const char *name;
	enum vb2_hash_algorithm algo = tlcl_get_family() == TPM_1 ?
		VB2_HASH_SHA1 : VB2_HASH_SHA256;
#if CONFIG(PAYLOAD_CDK2) && CONFIG(TPM2) && CONFIG(TPM_LOG_TPM2)
	struct vb2_hash hash;
	tpm_result_t rc;

	if (CONFIG(VBOOT_MOCK_SECDATA) && tpm_is_expected_absent())
		return TPM_SUCCESS;
	/* Vboot can emit PCR events before the first CBFS measurement. */
	rc = tspi_ensure_crtm_initialized();
	if (rc != TPM_SUCCESS)
		return rc;
	algo = TPM_MEASURE_ALGO;
#endif

	if (vb2api_get_pcr_digest(ctx, which_digest, buffer, &size) != VB2_SUCCESS)
		return TPM_CB_FAIL;

	switch (which_digest) {
	case BOOT_MODE_PCR:
#if CONFIG(PAYLOAD_CDK2) && CONFIG(TPM2) && CONFIG(TPM_LOG_TPM2)
		/* Preserve SHA1 and its historical zero-padded SHA256 representation. */
		if (algo != VB2_HASH_SHA1 && algo != VB2_HASH_SHA256) {
			uint8_t mode[] = {
				!!(ctx->flags & VB2_CONTEXT_DEVELOPER_MODE),
				!!(ctx->flags & VB2_CONTEXT_RECOVERY_MODE),
				!(ctx->flags & VB2_CONTEXT_RECOVERY_MODE),
			};

			if (vb2_hash_calculate(false, mode, sizeof(mode), algo, &hash))
				return TPM_CB_HASH_ERROR;
			memcpy(buffer, hash.raw, vb2_digest_size(algo));
		}
#endif
		name = TPM_PCR_BOOT_MODE;
		break;
	case HWID_DIGEST_PCR:
#if CONFIG(PAYLOAD_CDK2) && CONFIG(TPM2) && CONFIG(TPM_LOG_TPM2)
		if (algo != VB2_HASH_SHA256) {
			char hwid[VB2_GBB_HWID_MAX_SIZE];
			uint32_t hwid_size = sizeof(hwid);

			if (size != VB2_SHA256_DIGEST_SIZE ||
			    vb2api_gbb_read_hwid(ctx, hwid, &hwid_size) != VB2_SUCCESS ||
			    !hwid_size || hwid_size > sizeof(hwid))
				return TPM_CB_HASH_ERROR;
			/* GBB hashes the HWID without its terminating NUL. */
			if (vb2_hash_calculate(false, hwid, hwid_size - 1, VB2_HASH_SHA256, &hash) ||
			    memcmp(hash.raw, buffer, size) ||
			    vb2_hash_calculate(false, hwid, hwid_size - 1, algo, &hash))
				return TPM_CB_HASH_ERROR;
			memcpy(buffer, hash.raw, vb2_digest_size(algo));
		}
#endif
		name = TPM_PCR_GBB_HWID_NAME;
		break;
	case FIRMWARE_VERSION_PCR:
		/* The version is a raw value, zero-padded by vb2api_get_pcr_digest(). */
		name = TPM_PCR_FIRMWARE_VERSION;
		break;
	default:
		return TPM_CB_FAIL;
	}
	return tpm_extend_pcr(pcr, algo, buffer, vb2_digest_size(algo), name);
}
