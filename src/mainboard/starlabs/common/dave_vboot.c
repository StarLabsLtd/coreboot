/* SPDX-License-Identifier: GPL-2.0-only */

#include <security/tpm/tspi.h>
#include <security/vboot/misc.h>
#include <vb2_api.h>

static struct vb2_digest_context body_measurement;

vb2_error_t vboot_body_block(const struct vb2_context *ctx, const void *data,
			    size_t size, size_t offset, size_t total)
{
	if (ctx->flags & VB2_CONTEXT_S3_RESUME)
		return VB2_SUCCESS;

	if (!offset) {
		/* The vboot hash may already own the hardware hash engine. */
		vb2_error_t rc = vb2_digest_init(&body_measurement, false, VB2_HASH_SHA256, total);
		if (rc)
			return rc;
	}

	return vb2_digest_extend(&body_measurement, data, size);
}

vb2_error_t vboot_body_verified(const struct vb2_context *ctx)
{
	uint8_t digest[VB2_SHA256_DIGEST_SIZE];

	if (ctx->flags & VB2_CONTEXT_S3_RESUME)
		return VB2_SUCCESS;

	vb2_error_t rc = vb2_digest_finalize(&body_measurement, digest, sizeof(digest));
	if (rc)
		return rc;

	if (tpm_extend_pcr_required(2, VB2_HASH_SHA256, digest, sizeof(digest),
				    "VBOOT: verified RW body") != TPM_SUCCESS)
		return VB2_ERROR_UNKNOWN;

	return VB2_SUCCESS;
}
