/* SPDX-License-Identifier: GPL-2.0-only */

#include <commonlib/helpers.h>
#include <fmap.h>
#include <security/vboot/vboot_common.h>
#include <vb2_api.h>

#include <2common.h>
#include <2misc.h>
#include <2secdata.h>

/* Verify the other slot without changing the selected slot, NV data or PCRs. */
static uint32_t fallback_version(struct vb2_context *ctx, uint32_t proposed)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	struct vb2_gbb_header *gbb = vb2_get_gbb(ctx);
	struct vb2_workbuf wb;
	struct vb2_public_key root, key;
	struct vb2_keyblock *block;
	struct vb2_fw_preamble *pre;
	struct region_device vblock, body;
	struct vb2_digest_context digest;
	struct vb2_hash hash;
	uint8_t block_data[CONFIG_VBOOT_HASH_BLOCK_SIZE];
	uint8_t *root_data, *key_data;
	uint32_t size, key_size, pre_size, version;
	size_t offset;
	uint32_t floor = sd->fw_version_secdata;
	vb2_error_t rv;

	if (fmap_locate_area_as_rdev(sd->fw_slot ? "VBLOCK_A" : "VBLOCK_B", &vblock) ||
	    fmap_locate_area_as_rdev(sd->fw_slot ? "FW_MAIN_A" : "FW_MAIN_B", &body))
		return floor;

	vb2_workbuf_from_ctx(ctx, &wb);
	root_data = vb2_workbuf_alloc(&wb, gbb->rootkey_size);
	if (!root_data || vb2ex_read_resource(ctx, VB2_RES_GBB, gbb->rootkey_offset,
					    root_data, gbb->rootkey_size) ||
	    vb2_unpack_key_buffer(&root, root_data, gbb->rootkey_size))
		return floor;
	root.allow_hwcrypto = vb2api_hwcrypto_allowed(ctx);

	/* Signature verification modifies its input, so use mutable workbuf copies. */
	block = vb2_workbuf_alloc(&wb, sizeof(*block));
	if (!block || rdev_readat(&vblock, block, 0, sizeof(*block)) != sizeof(*block))
		return floor;
	size = block->keyblock_size;
	if (size < sizeof(*block) || size > region_device_sz(&vblock))
		return floor;
	block = vb2_workbuf_realloc(&wb, sizeof(*block), size);
	if (!block || rdev_readat(&vblock, block, 0, size) != size ||
	    vb2_verify_keyblock(block, size, &root, &wb) ||
	    vb2_verify_packed_key_inside(block, size, &block->data_key) ||
	    block->data_key.key_version > VB2_MAX_KEY_VERSION)
		return floor;
	version = block->data_key.key_version << 16;
	key_size = block->data_key.key_offset + block->data_key.key_size;

	/* Retain only the authenticated data key; recycle root/keyblock scratch. */
	vb2_workbuf_from_ctx(ctx, &wb);
	key_data = vb2_workbuf_alloc(&wb, key_size);
	if (!key_data)
		return floor;
	memmove(key_data, &block->data_key, key_size);
	if (vb2_unpack_key_buffer(&key, key_data, key_size))
		return floor;
	key.allow_hwcrypto = root.allow_hwcrypto;

	pre = vb2_workbuf_alloc(&wb, sizeof(*pre));
	if (!pre || rdev_readat(&vblock, pre, size, sizeof(*pre)) != sizeof(*pre))
		return floor;
	pre_size = pre->preamble_size;
	if (pre_size < sizeof(*pre) || pre_size > region_device_sz(&vblock) - size)
		return floor;
	pre = vb2_workbuf_realloc(&wb, sizeof(*pre), pre_size);
	if (!pre || rdev_readat(&vblock, pre, size, pre_size) != pre_size ||
	    vb2_verify_fw_preamble(pre, pre_size, &key, &wb) ||
	    pre->firmware_version > VB2_MAX_PREAMBLE_VERSION ||
	    !pre->body_signature.data_size ||
	    pre->body_signature.data_size > region_device_sz(&body))
		return floor;
	version |= pre->firmware_version;
	version = MIN(version, proposed);
	if (version <= floor)
		return floor;

	/* A signed header alone may belong to an interrupted, incomplete update. */
	/* Native verification covers the signed body, not unused slot capacity. */
	rv = vb2_digest_init(&digest, key.allow_hwcrypto, key.hash_alg,
		pre->body_signature.data_size);
	if (rv)
		return floor;
	for (offset = 0; offset < pre->body_signature.data_size;) {
		size_t length = MIN(sizeof(block_data),
			pre->body_signature.data_size - offset);

		if (rdev_readat(&body, block_data, offset, length) != length ||
		    vb2_digest_extend(&digest, block_data, length))
			return floor;
		offset += length;
	}
	if (vb2_digest_finalize(&digest, hash.raw, vb2_digest_size(key.hash_alg)) ||
	    vb2_verify_digest(&key, &pre->body_signature, hash.raw, &wb))
		return floor;
	return version;
}

vb2_error_t vboot_verify_firmware(struct vb2_context *ctx)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	uint32_t floor = sd->fw_version_secdata;
	bool changed = ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED;
	vb2_error_t rv = vb2api_fw_phase3(ctx);

	if (CONFIG(VBOOT_SLOTS_RW_AB) && !CONFIG(VBOOT_CBFS_INTEGRATION)) {
		/* Defer the library's single-slot raise until both bodies are checked. */
		vb2_secdata_firmware_set(ctx, VB2_SECDATA_FIRMWARE_VERSIONS, floor);
		sd->fw_version_secdata = floor;
		if (!changed)
			ctx->flags &= ~VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED;
	}
	return rv;
}

void vboot_update_firmware_floor(struct vb2_context *ctx)
{
	struct vb2_shared_data *sd = vb2_get_sd(ctx);
	uint32_t floor;

	if (!CONFIG(VBOOT_SLOTS_RW_AB) || CONFIG(VBOOT_CBFS_INTEGRATION) ||
	    ctx->flags & (VB2_CONTEXT_S3_RESUME | VB2_CONTEXT_RECOVERY_MODE) ||
	    sd->last_fw_slot != sd->fw_slot ||
	    sd->last_fw_result != VB2_FW_RESULT_SUCCESS ||
	    sd->fw_version <= sd->fw_version_secdata)
		return;

	floor = fallback_version(ctx, sd->fw_version);
	if (floor > sd->fw_version_secdata) {
		vb2_secdata_firmware_set(ctx, VB2_SECDATA_FIRMWARE_VERSIONS, floor);
		sd->fw_version_secdata = floor;
	}
}
