/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootmem.h>
#include <cbfs.h>
#include <console/console.h>
#include <program_loading.h>
#include <security/tpm/tspi.h>
#include <security/vboot/misc.h>
#include <string.h>
#include <vb2_api.h>

static void *payload_buffer(void *arg, size_t size, const union cbfs_mdata *mdata)
{
	if (!size || size > SIZE_MAX - 4095)
		return NULL;

	return bootmem_allocate_buffer(size);
}

bool platform_skip_payload_preload(void)
{
	return true;
}

bool platform_load_payload(struct prog *payload)
{
	uint8_t evidence[1 + VB2_SHA256_DIGEST_SIZE];
	struct region source;
	struct vb2_hash hash;
	size_t size;

	if (vboot_get_context()->flags & VB2_CONTEXT_S3_RESUME)
		return false;

	void *snapshot = cbfs_alloc_snapshot(prog_name(payload), CBFS_TYPE_SELF, payload_buffer,
					     NULL, &size, &source);
	if (!snapshot)
		die("Unable to read the DAVE payload snapshot\n");

	const struct cbfs_boot_device *rw = vboot_get_cbfs_boot_device();
	evidence[0] = rw && region_is_subregion(region_device_region(&rw->rdev), &source);

	if (vb2_hash_calculate(vboot_hwcrypto_allowed(), snapshot, size, VB2_HASH_SHA256, &hash))
		die("Unable to hash the DAVE payload\n");

	memcpy(evidence + 1, hash.raw, VB2_SHA256_DIGEST_SIZE);
	if (vb2_hash_calculate(false, evidence, sizeof(evidence), VB2_HASH_SHA256, &hash) ||
	    tpm_extend_pcr_required(2, VB2_HASH_SHA256, hash.raw, VB2_SHA256_DIGEST_SIZE,
				    "VBOOT: payload origin and digest") != TPM_SUCCESS)
		die("Unable to measure the DAVE payload\n");

	payload->cbfs_type = CBFS_TYPE_SELF;
	if (!selfload_bounded(payload, snapshot, size))
		die("Unable to load the DAVE payload snapshot\n");

	return true;
}
