/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <security/vboot/firmware_floor.c>

static uint8_t workbuf[VB2_FIRMWARE_WORKBUF_RECOMMENDED_SIZE]
	__aligned(VB2_WORKBUF_ALIGN);
static uint8_t vblock_data[512];
static uint8_t body_data[3 * CONFIG_VBOOT_HASH_BLOCK_SIZE + 23];
static struct vb2_context *ctx;
static struct vb2_shared_data *sd;
static struct vb2_gbb_header gbb;
static bool missing, short_read, bad_keyblock, bad_preamble, bad_body;
static bool short_body_read;
static unsigned int body_checks;
static uint32_t signed_size;
static struct vb2_digest_context *active_digest;
static uint32_t digest_bytes;
static unsigned int body_reads, digest_inits, digest_extends, digest_finalizes;
static bool digest_finalized;
static enum {
	DIGEST_SUCCESS,
	DIGEST_INIT_FAILURE,
	DIGEST_EXTEND_FAILURE,
	DIGEST_FINALIZE_FAILURE,
} digest_failure;
static const uint8_t expected_digest[VB2_SHA256_DIGEST_SIZE] = "checked body digest";

void vb2ex_printf(const char *func, const char *fmt, ...)
{
}
void vb2ex_abort(void)
{
	fail();
	__builtin_unreachable();
}

struct vb2_gbb_header *vb2_get_gbb(struct vb2_context *unused)
{
	return &gbb;
}

bool vb2api_hwcrypto_allowed(struct vb2_context *unused)
{
	return false;
}

void vb2_workbuf_from_ctx(struct vb2_context *unused, struct vb2_workbuf *wb)
{
	vb2_workbuf_init(wb, workbuf + sd->workbuf_used,
			 sizeof(workbuf) - sd->workbuf_used);
}

vb2_error_t vb2ex_read_resource(struct vb2_context *unused,
		enum vb2_resource_index index, uint32_t offset, void *buf, uint32_t size)
{
	assert_int_equal(index, VB2_RES_GBB);
	memset(buf, 0, size);
	return VB2_SUCCESS;
}

int fmap_locate_area_as_rdev(const char *name, struct region_device *rdev)
{
	assert_string_equal(name, name[0] == 'V' ?
		(sd->fw_slot ? "VBLOCK_A" : "VBLOCK_B") :
		(sd->fw_slot ? "FW_MAIN_A" : "FW_MAIN_B"));
	if (missing)
		return -1;
	*rdev = (struct region_device) { .region = { .offset = name[0] == 'V',
		.size = name[0] == 'V' ? sizeof(vblock_data) : sizeof(body_data) } };
	return 0;
}

ssize_t rdev_readat(const struct region_device *rdev, void *buf, size_t offset,
		   size_t size)
{
	bool vblock = region_device_offset(rdev) == 1;
	size_t capacity = vblock ? sizeof(vblock_data) : sizeof(body_data);

	assert_true(offset <= capacity);
	assert_true(size <= capacity - offset);
	memcpy(buf, (vblock ? vblock_data : body_data) + offset, size);
	if (vblock)
		return short_read ? size - 1 : size;
	assert_int_equal(offset, digest_bytes);
	body_reads++;
	return short_body_read && body_reads == 2 ? size - 1 : size;
}

vb2_error_t vb2_unpack_key_buffer(struct vb2_public_key *key, const uint8_t *buf,
				uint32_t size)
{
	memset(key, 0, sizeof(*key));
	key->hash_alg = VB2_HASH_SHA256;
	return VB2_SUCCESS;
}

vb2_error_t vb2_verify_keyblock(struct vb2_keyblock *block, uint32_t size,
		const struct vb2_public_key *key, const struct vb2_workbuf *wb)
{
	return bad_keyblock ? VB2_ERROR_UNKNOWN : VB2_SUCCESS;
}

vb2_error_t vb2_verify_fw_preamble(struct vb2_fw_preamble *pre, uint32_t size,
		const struct vb2_public_key *key, const struct vb2_workbuf *wb)
{
	return bad_preamble ? VB2_ERROR_UNKNOWN : VB2_SUCCESS;
}

/* Check the stream and digest handoff; signature validity remains a policy input. */
vb2_error_t vb2_digest_init(struct vb2_digest_context *digest, bool hwcrypto,
		enum vb2_hash_algorithm algorithm, uint32_t size)
{
	assert_false(hwcrypto);
	assert_int_equal(algorithm, VB2_HASH_SHA256);
	assert_int_equal(size, signed_size);
	active_digest = digest;
	digest_bytes = 0;
	digest_finalized = false;
	digest_inits++;
	return digest_failure == DIGEST_INIT_FAILURE ? VB2_ERROR_UNKNOWN : VB2_SUCCESS;
}

vb2_error_t vb2_digest_extend(struct vb2_digest_context *digest, const uint8_t *buf,
		uint32_t size)
{
	assert_ptr_equal(digest, active_digest);
	assert_true(size > 0 && size <= CONFIG_VBOOT_HASH_BLOCK_SIZE);
	assert_true(digest_bytes <= signed_size);
	assert_true(size <= signed_size - digest_bytes);
	assert_memory_equal(buf, body_data + digest_bytes, size);
	digest_extends++;
	if (digest_failure == DIGEST_EXTEND_FAILURE && digest_extends == 2)
		return VB2_ERROR_UNKNOWN;
	digest_bytes += size;
	return VB2_SUCCESS;
}

size_t vb2_digest_size(enum vb2_hash_algorithm algorithm)
{
	assert_int_equal(algorithm, VB2_HASH_SHA256);
	return sizeof(expected_digest);
}

vb2_error_t vb2_digest_finalize(struct vb2_digest_context *digest, uint8_t *output,
		uint32_t size)
{
	assert_ptr_equal(digest, active_digest);
	assert_int_equal(digest_bytes, signed_size);
	assert_int_equal(size, sizeof(expected_digest));
	digest_finalizes++;
	if (digest_failure == DIGEST_FINALIZE_FAILURE)
		return VB2_ERROR_UNKNOWN;
	memcpy(output, expected_digest, size);
	digest_finalized = true;
	return VB2_SUCCESS;
}

vb2_error_t vb2_verify_digest(const struct vb2_public_key *key,
		struct vb2_signature *sig, const uint8_t *digest,
		const struct vb2_workbuf *wb)
{
	assert_true(digest_finalized);
	assert_int_equal(sig->data_size, signed_size);
	assert_memory_equal(digest, expected_digest, sizeof(expected_digest));
	body_checks++;
	return bad_body ? VB2_ERROR_UNKNOWN : VB2_SUCCESS;
}

/* Model the existing library's single-slot raise, not the new policy. */
vb2_error_t vb2api_fw_phase3(struct vb2_context *unused)
{
	sd->fw_version_secdata = sd->fw_version;
	vb2_secdata_firmware_set(ctx, VB2_SECDATA_FIRMWARE_VERSIONS, sd->fw_version);
	return VB2_SUCCESS;
}

static void set_other_version(uint32_t version)
{
	struct vb2_keyblock block = {
		.keyblock_size = 256,
		.data_key = { .key_version = version >> 16,
			.key_offset = sizeof(struct vb2_packed_key), .key_size = 16 },
	};
	struct vb2_fw_preamble pre = {
		.preamble_size = sizeof(pre),
		.firmware_version = version & 0xffff,
		.body_signature.data_size = sizeof(body_data),
	};

	memcpy(vblock_data, &block, sizeof(block));
	memcpy(vblock_data + 256, &pre, sizeof(pre));
}

static int setup(void **state)
{
	memset(workbuf, 0, sizeof(workbuf));
	memset(vblock_data, 0, sizeof(vblock_data));
	for (size_t i = 0; i < sizeof(body_data); i++)
		body_data[i] = (i * 13 + 7) ^ (i >> 8);
	sd = (void *)workbuf;
	ctx = (void *)(workbuf + offsetof(struct vb2_shared_data, ctx));
	sd->workbuf_used = vb2_wb_round_up(sizeof(*sd));
	sd->workbuf_size = sizeof(workbuf);
	vb2api_secdata_firmware_create(ctx);
	assert_int_equal(vb2_secdata_firmware_init(ctx), VB2_SUCCESS);
	vb2_secdata_firmware_set(ctx, VB2_SECDATA_FIRMWARE_VERSIONS, 0x10007);
	sd->fw_version_secdata = 0x10007;
	sd->fw_version = 0x10009;
	sd->last_fw_result = VB2_FW_RESULT_SUCCESS;
	ctx->flags = 0;
	gbb.rootkey_size = 64;
	missing = short_read = bad_keyblock = bad_preamble = bad_body = false;
	short_body_read = false;
	digest_finalized = false;
	body_checks = 0;
	body_reads = 0;
	digest_inits = 0;
	digest_extends = 0;
	digest_finalizes = 0;
	digest_bytes = 0;
	active_digest = NULL;
	digest_failure = DIGEST_SUCCESS;
	signed_size = sizeof(body_data);
	set_other_version(0x10007);
	return 0;
}

static void expect_floor(uint32_t expected)
{
	vboot_update_firmware_floor(ctx);
	assert_int_equal(sd->fw_version_secdata, expected);
	assert_int_equal(vb2_secdata_firmware_get(ctx, VB2_SECDATA_FIRMWARE_VERSIONS),
			 expected);
	assert_int_equal(vb2api_secdata_firmware_check(ctx), VB2_SUCCESS);
}

static void older_fallback(void **state)
{
	assert_int_equal(vboot_verify_firmware(ctx), VB2_SUCCESS);
	assert_int_equal(sd->fw_version_secdata, 0x10007);
	assert_false(ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
	expect_floor(0x10007);
	assert_int_equal(body_checks, 0);
}

static void both_newer(void **state)
{
	set_other_version(0x10008);
	expect_floor(0x10008);
	assert_true(ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
	assert_int_equal(body_checks, 1);
}

static void selected_is_older(void **state)
{
	set_other_version(0x1000a);
	expect_floor(0x10009);
}

static void composite_version(void **state)
{
	sd->fw_version = 0x20001;
	set_other_version(0x1ffff);
	expect_floor(0x1ffff);
}

static void unusable_fallback(void **state)
{
	set_other_version(0x10008);
	missing = true;
	expect_floor(0x10007);
	missing = false;
	short_read = true;
	expect_floor(0x10007);
	short_read = false;
	bad_keyblock = true;
	expect_floor(0x10007);
	bad_keyblock = false;
	bad_preamble = true;
	expect_floor(0x10007);
	bad_preamble = false;
	bad_body = true;
	expect_floor(0x10007);
}

static void no_lowering(void **state)
{
	set_other_version(0x10006);
	expect_floor(0x10007);
}

static void no_success_no_advance(void **state)
{
	set_other_version(0x10008);
	sd->last_fw_result = VB2_FW_RESULT_TRYING;
	expect_floor(0x10007);
	sd->last_fw_result = VB2_FW_RESULT_SUCCESS;
	sd->last_fw_slot = 1;
	expect_floor(0x10007);
}

static void resume_and_recovery(void **state)
{
	set_other_version(0x10008);
	ctx->flags |= VB2_CONTEXT_S3_RESUME;
	expect_floor(0x10007);
	ctx->flags = VB2_CONTEXT_RECOVERY_MODE;
	expect_floor(0x10007);
}

static void preserve_pending_secdata(void **state)
{
	ctx->flags = VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED;
	assert_int_equal(vboot_verify_firmware(ctx), VB2_SUCCESS);
	assert_true(ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
	assert_int_equal(sd->fw_version_secdata, 0x10007);
}

static void other_slot_direction(void **state)
{
	sd->fw_slot = sd->last_fw_slot = 1;
	set_other_version(0x10008);
	expect_floor(0x10008);
}

static void signed_prefix(void **state)
{
	struct vb2_fw_preamble pre;

	set_other_version(0x10008);
	memcpy(&pre, vblock_data + 256, sizeof(pre));
	signed_size = pre.body_signature.data_size = 32;
	memcpy(vblock_data + 256, &pre, sizeof(pre));
	expect_floor(0x10008);
	assert_int_equal(body_checks, 1);
}

static void invalid_signed_size(void **state)
{
	struct vb2_fw_preamble pre;

	set_other_version(0x10008);
	memcpy(&pre, vblock_data + 256, sizeof(pre));
	pre.body_signature.data_size = 0;
	memcpy(vblock_data + 256, &pre, sizeof(pre));
	expect_floor(0x10007);
	pre.body_signature.data_size = sizeof(body_data) + 1;
	memcpy(vblock_data + 256, &pre, sizeof(pre));
	expect_floor(0x10007);
	assert_int_equal(body_checks, 0);
}

static void signed_prefix_across_chunks(void **state)
{
	struct vb2_fw_preamble pre;

	set_other_version(0x10008);
	memcpy(&pre, vblock_data + 256, sizeof(pre));
	signed_size = 2 * CONFIG_VBOOT_HASH_BLOCK_SIZE + 13;
	pre.body_signature.data_size = signed_size;
	memcpy(vblock_data + 256, &pre, sizeof(pre));
	expect_floor(0x10008);
	assert_true(body_reads > 1);
	assert_int_equal(digest_inits, 1);
	assert_int_equal(digest_extends, body_reads);
	assert_int_equal(digest_bytes, signed_size);
	assert_int_equal(digest_finalizes, 1);
	assert_int_equal(body_checks, 1);
}

static void short_body_does_not_advance(void **state)
{
	set_other_version(0x10008);
	short_body_read = true;
	expect_floor(0x10007);
	assert_int_equal(body_reads, 2);
	assert_int_equal(digest_extends, 1);
	assert_true(digest_bytes > 0 && digest_bytes < signed_size);
	assert_int_equal(digest_finalizes, 0);
	assert_int_equal(body_checks, 0);
	assert_false(ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
}

static void failed_digest_init_does_not_advance(void **state)
{
	set_other_version(0x10008);
	digest_failure = DIGEST_INIT_FAILURE;
	expect_floor(0x10007);
	assert_int_equal(digest_inits, 1);
	assert_int_equal(body_reads, 0);
	assert_int_equal(digest_extends, 0);
	assert_int_equal(digest_finalizes, 0);
	assert_int_equal(body_checks, 0);
	assert_false(ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
}

static void failed_digest_extend_does_not_advance(void **state)
{
	set_other_version(0x10008);
	digest_failure = DIGEST_EXTEND_FAILURE;
	expect_floor(0x10007);
	assert_int_equal(body_reads, 2);
	assert_int_equal(digest_extends, 2);
	assert_true(digest_bytes > 0 && digest_bytes < signed_size);
	assert_int_equal(digest_finalizes, 0);
	assert_int_equal(body_checks, 0);
	assert_false(ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
}

static void failed_digest_finalize_does_not_advance(void **state)
{
	set_other_version(0x10008);
	digest_failure = DIGEST_FINALIZE_FAILURE;
	expect_floor(0x10007);
	assert_int_equal(digest_bytes, signed_size);
	assert_int_equal(digest_finalizes, 1);
	assert_int_equal(body_checks, 0);
	assert_false(ctx->flags & VB2_CONTEXT_SECDATA_FIRMWARE_CHANGED);
}

static void unchanged_policy(void **state)
{
	assert_int_equal(vboot_verify_firmware(ctx), VB2_SUCCESS);
	assert_int_equal(sd->fw_version_secdata, 0x10009);
	expect_floor(0x10009);
	assert_int_equal(body_checks, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(older_fallback, setup),
		cmocka_unit_test_setup(both_newer, setup),
		cmocka_unit_test_setup(selected_is_older, setup),
		cmocka_unit_test_setup(composite_version, setup),
		cmocka_unit_test_setup(unusable_fallback, setup),
		cmocka_unit_test_setup(no_lowering, setup),
		cmocka_unit_test_setup(no_success_no_advance, setup),
		cmocka_unit_test_setup(resume_and_recovery, setup),
		cmocka_unit_test_setup(preserve_pending_secdata, setup),
		cmocka_unit_test_setup(other_slot_direction, setup),
		cmocka_unit_test_setup(signed_prefix, setup),
		cmocka_unit_test_setup(invalid_signed_size, setup),
		cmocka_unit_test_setup(signed_prefix_across_chunks, setup),
		cmocka_unit_test_setup(short_body_does_not_advance, setup),
		cmocka_unit_test_setup(failed_digest_init_does_not_advance, setup),
		cmocka_unit_test_setup(failed_digest_extend_does_not_advance, setup),
		cmocka_unit_test_setup(failed_digest_finalize_does_not_advance, setup),
	};
	const struct CMUnitTest compatibility[] = {
		cmocka_unit_test_setup(unchanged_policy, setup),
	};
	if (!CONFIG(VBOOT_SLOTS_RW_AB) || CONFIG(VBOOT_CBFS_INTEGRATION))
		return cb_run_group_tests(compatibility, NULL, NULL);
	return cb_run_group_tests(tests, NULL, NULL);
}
