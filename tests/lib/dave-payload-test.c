/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbfs.h>
#include <commonlib/bsd/cbfs_private.h>
#include <program_loading.h>
#include <tests/lib/cbfs_util.h>
#include <tests/test.h>
#define main coreboot_main
#include <security/tpm/preboot_auth.c>
#undef main

/* Exercise the production loader, CBFS traversal and vboot software SHA-256. */
static struct cbfs_boot_device rw_cbfs, ro_cbfs;
static struct cbfs_test_file rw_file, ro_file;
static uint8_t rw_cache[16] __aligned(CBFS_MCACHE_ALIGNMENT);
static uint8_t ro_cache[512] __aligned(CBFS_MCACHE_ALIGNMENT);
static struct vb2_hash metadata_hash;
static struct vb2_context context;
static struct prog *loaded_payload;
static const void *expected_mapping;
static bool rw_available, alternate_payload;
static unsigned int hook_calls, selfload_calls, phase_opens;

const struct cbfs_boot_device *vboot_get_cbfs_boot_device(void)
{
	return rw_available ? &rw_cbfs : NULL;
}

const struct cbfs_boot_device *cbfs_get_boot_device(bool force_ro)
{
	return force_ro || !rw_available ? &ro_cbfs : &rw_cbfs;
}

struct vb2_context *vboot_get_context(void)
{
	return &context;
}

vb2_error_t vb2api_get_metadata_hash(struct vb2_context *ctx, struct vb2_hash **hash)
{
	assert_ptr_equal(ctx, &context);
	*hash = &metadata_hash;
	return VB2_SUCCESS;
}

bool vb2api_hwcrypto_allowed(struct vb2_context *ctx)
{
	return 0;
}

int vboot_recovery_mode_enabled(void)
{
	return 0;
}

int platform_is_resuming(void)
{
	return 0;
}

void die(const char *message, ...)
{
	mock_assert(false, message, __FILE__, __LINE__);
	__builtin_unreachable();
}

void vboot_fail_and_reboot(struct vb2_context *ctx, uint8_t reason, uint8_t subcode)
{
	assert_ptr_equal(ctx, &context);
	assert_int_equal(reason, VB2_RECOVERY_FW_BODY);
	die("Payload hash mismatch");
}

void vb2ex_printf(const char *func, const char *fmt, ...) { }
void vb2ex_abort(void) { die("Unexpected vboot abort"); }
void post_code(u8 value) { }

vb2_error_t vb2ex_hwcrypto_digest_init(enum vb2_hash_algorithm hash_alg, uint32_t data_size)
{
	return VB2_ERROR_EX_HWCRYPTO_UNSUPPORTED;
}

vb2_error_t vb2ex_hwcrypto_digest_extend(const uint8_t *buf, uint32_t size)
{
	fail_msg("Hardware digest must not be used");
	return VB2_ERROR_EX_HWCRYPTO_UNSUPPORTED;
}

vb2_error_t vb2ex_hwcrypto_digest_finalize(uint8_t *digest, uint32_t digest_size)
{
	fail_msg("Hardware digest must not be used");
	return VB2_ERROR_EX_HWCRYPTO_UNSUPPORTED;
}

size_t ulzman(const void *src, size_t srcn, void *dst, size_t dstn)
{
	fail_msg("Uncompressed fixture must not use LZMA");
	return 0;
}

size_t ulz4fn(const void *src, size_t srcn, void *dst, size_t dstn)
{
	fail_msg("Uncompressed fixture must not use LZ4");
	return 0;
}

int prog_locate_hook(struct prog *payload)
{
	hook_calls++;
	loaded_payload = payload;
	if (alternate_payload)
		prog_set_entry(payload, (void *)0x1234, NULL);
	return alternate_payload;
}

bool selfload_mapped(struct prog *payload, void *mapping, enum bootmem_type dest_type)
{
	assert_ptr_equal(mapping, expected_mapping);
	assert_int_equal(dest_type, BM_MEM_RAM);
	selfload_calls++;
	loaded_payload = payload;
	prog_set_entry(payload, (void *)0x1234, NULL);
	return true;
}

tpm_result_t tpm_extend_pcr_compact_hash(int pcr, enum vb2_hash_algorithm algo,
				      const uint8_t *digest, size_t digest_len,
				      const char *digest_name)
{
	assert_int_equal(pcr, 6);
	assert_int_equal(algo, VB2_HASH_SHA256);
	assert_int_equal(digest_len, sizeof(preboot_auth_open));
	assert_memory_equal(digest, preboot_auth_open, digest_len);
	phase_opens++;
	return TPM_SUCCESS;
}

static void prepare_metadata(void)
{
	if (CONFIG(TOCTOU_SAFETY)) {
		metadata_hash.algo = VB2_HASH_SHA256;
		assert_int_equal(cbfs_walk(&rw_cbfs.rdev, NULL, NULL, &metadata_hash,
					  CBFS_WALK_WRITEBACK_HASH), CB_CBFS_NOT_FOUND);
	}
	/* Force the production TOCTOU-safe lookup path after mcache overflow. */
	assert_int_equal(cbfs_mcache_build(&rw_cbfs.rdev, rw_cache, sizeof(rw_cache), NULL),
			 CB_CBFS_CACHE_FULL);
}

static int setup(void **state)
{
	if (loaded_payload)
		prog_set_entry(loaded_payload, NULL, NULL);
	memset(&context, 0, sizeof(context));
	rw_file = ro_file = file_valid_hash;
	rw_file.header.type = ro_file.header.type = cpu_to_be32(CBFS_TYPE_SELF);
	strcpy((char *)rw_file.filename, CONFIG_CBFS_PREFIX "/payload");
	strcpy((char *)ro_file.filename, CONFIG_CBFS_PREFIX "/payload");
	assert_int_equal(rdev_chain_mem(&rw_cbfs.rdev, &rw_file, sizeof(rw_file)), 0);
	assert_int_equal(rdev_chain_mem(&ro_cbfs.rdev, &ro_file, sizeof(ro_file)), 0);
	rw_cbfs.mcache = rw_cache;
	rw_cbfs.mcache_size = sizeof(rw_cache);
	ro_cbfs.mcache = ro_cache;
	ro_cbfs.mcache_size = sizeof(ro_cache);
	prepare_metadata();
	assert_int_equal(cbfs_mcache_build(&ro_cbfs.rdev, ro_cache, sizeof(ro_cache), NULL),
			 CB_SUCCESS);
	expected_mapping = &rw_file.attrs_and_data[HASH_ATTR_SIZE];
	rw_available = true;
	alternate_payload = false;
	hook_calls = selfload_calls = phase_opens = 0;
	return 0;
}

static void load_then_open_phase(void)
{
	/* hardwaremain advances from BS_PAYLOAD_LOAD to BS_PAYLOAD_BOOT only on return. */
	payload_load();
	open_preboot_auth_phase(NULL);
}

static void expect_load_failure(void)
{
	expect_assert_failure(load_then_open_phase());
	assert_int_equal(selfload_calls, 0);
	assert_int_equal(phase_opens, 0);
}

static void verified_rw_payload(void **state)
{
	load_then_open_phase();
	assert_int_equal(selfload_calls, 1);
	assert_int_equal(phase_opens, 1);
}

static void missing_rw_payload(void **state)
{
	strcpy((char *)rw_file.filename, "other-file");
	prepare_metadata();
	if (CONFIG(DAVE_MODE)) {
		expect_load_failure();
	} else {
		expected_mapping = &ro_file.attrs_and_data[HASH_ATTR_SIZE];
		load_then_open_phase();
		assert_int_equal(selfload_calls, 1);
		assert_int_equal(phase_opens, 1);
	}
}

static void no_selected_rw(void **state)
{
	rw_available = false;
	if (CONFIG(DAVE_MODE)) {
		expect_load_failure();
	} else {
		expected_mapping = &ro_file.attrs_and_data[HASH_ATTR_SIZE];
		load_then_open_phase();
		assert_int_equal(phase_opens, 1);
	}
}

#if CONFIG(CBFS_VERIFICATION)
static void corrupted_payload_data(void **state)
{
	rw_file.attrs_and_data[HASH_ATTR_SIZE] ^= 1;
	expect_load_failure();
}
#endif

#if CONFIG(TOCTOU_SAFETY)
static void corrupted_payload_metadata(void **state)
{
	rw_file.header.type = cpu_to_be32(CBFS_TYPE_RAW);
	expect_load_failure();
}
#endif

#if CONFIG(DAVE_MODE)
static void failed_reload_cannot_reuse_entry(void **state)
{
	load_then_open_phase();
	strcpy((char *)rw_file.filename, "other-file");
	prepare_metadata();
	selfload_calls = phase_opens = 0;
	expect_load_failure();
}
#endif

static void alternate_provider_cannot_bypass_rw(void **state)
{
	alternate_payload = true;
	rw_available = false;
	if (CONFIG(DAVE_MODE)) {
		expect_load_failure();
		assert_int_equal(hook_calls, 0);
	} else {
		load_then_open_phase();
		assert_int_equal(hook_calls, 1);
		assert_int_equal(selfload_calls, 0);
		assert_int_equal(phase_opens, 1);
	}
}

static void ordinary_cbfs_fallback_remains(void **state)
{
	strcpy((char *)rw_file.filename, "other-file");
	prepare_metadata();
	void *mapping = cbfs_map(CONFIG_CBFS_PREFIX "/payload", NULL);

	assert_ptr_equal(mapping, &ro_file.attrs_and_data[HASH_ATTR_SIZE]);
	cbfs_unmap(mapping);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(verified_rw_payload, setup),
		cmocka_unit_test_setup(missing_rw_payload, setup),
		cmocka_unit_test_setup(no_selected_rw, setup),
#if CONFIG(CBFS_VERIFICATION)
		cmocka_unit_test_setup(corrupted_payload_data, setup),
#endif
#if CONFIG(TOCTOU_SAFETY)
		cmocka_unit_test_setup(corrupted_payload_metadata, setup),
#endif
#if CONFIG(DAVE_MODE)
		cmocka_unit_test_setup(failed_reload_cannot_reuse_entry, setup),
#endif
		cmocka_unit_test_setup(alternate_provider_cannot_bypass_rw, setup),
		cmocka_unit_test_setup(ordinary_cbfs_fallback_remains, setup),
	};

	return cb_run_group_tests(tests, NULL, NULL);
}
