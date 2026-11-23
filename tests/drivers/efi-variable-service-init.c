/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <drivers/efi/option.h>
#include <option.h>
#include <tests/test.h>
#if CONFIG(VBOOT_SLOTS_RW_AB)
#include <2nvstorage_fields.h>
#endif
#include <Uefi/UefiBaseType.h>
#include <Guid/VariableFormat.h>
#include <Pi/PiFirmwareVolume.h>

#include <drivers/efi_variable_service/smi.c>

static uint8_t flash[0x80000];
static uint8_t transport[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
static struct region_device flash_device;
static struct efi_variable_service_state state;
static bool owner_failure, writer_ready;
static unsigned int resumes;
static bool resume;
static bool read_error, write_error, readback_error;
static unsigned int writes, starts;
static unsigned int reads, gets;
static bool boot_access;
static uint32_t cached_value, cached_size, cached_status, cached_attributes, native_result;
static enum {
	REPLY_VALID, REPLY_FUNCTION,
	REPLY_NAME_SIZE, REPLY_DATA_SIZE, REPLY_GUID, REPLY_NAME,
} reply_fault;
static enum { QUERY_VALID, QUERY_ERROR, QUERY_SIZE, QUERY_VALUE } query_fault;

static ssize_t flash_read(const struct region_device *rdev, void *buf, size_t offset,
			  size_t size)
{
	reads++;
	if (read_error)
		return -1;
	memcpy(buf, flash + offset, size);
	return size;
}

static ssize_t flash_write(const struct region_device *rdev, const void *buf, size_t offset,
			   size_t size)
{
	writes++;
	if (write_error)
		return -1;
	for (size_t i = 0; i < size; i++) {
		assert_int_equal(flash[offset + i] & ((const uint8_t *)buf)[i],
				 ((const uint8_t *)buf)[i]);
		flash[offset + i] &= ((const uint8_t *)buf)[i];
	}
	if (readback_error)
		read_error = true;
	return size;
}

static ssize_t flash_erase(const struct region_device *rdev, size_t offset, size_t size)
{
	if (write_error)
		return -1;
	memset(flash + offset, 0xff, size);
	return size;
}

static const struct region_device_ops flash_ops = {
	.readat = flash_read,
	.writeat = flash_write,
	.eraseat = flash_erase,
};

#if CONFIG(VBOOT_SLOTS_RW_AB)
const struct region_device *boot_device_ro(void) { return &flash_device; }
const struct region_device *boot_device_rw(void) { return &flash_device; }
size_t fast_spi_get_bios_region(size_t *size) { *size = sizeof(flash); return 0; }
static uint8_t nvdata[VB2_NVDATA_SIZE];
static unsigned int nv_writes;
void read_vbnv_flash(uint8_t *data) { memcpy(data, nvdata, sizeof(nvdata)); }
int save_vbnv_flash(const uint8_t *data)
{
	memcpy(nvdata, data, sizeof(nvdata));
	nv_writes++;
	return 0;
}
void vb2ex_printf(const char *function, const char *format, ...) { }
#endif

const struct region_device *efi_variable_service_write_device(void)
{
	return writer_ready ? &flash_device : NULL;
}

int efi_variable_service_bind_spi(struct efi_variable_service_state *handoff)
{
	return 0;
}

int smm_subregion(int sub, uintptr_t *base, size_t *size)
{
	assert_int_equal(sub, SMM_SUBREGION_EFI_VARIABLE_SERVICE);
	*base = (uintptr_t)&state;
	*size = 4 * MiB;
	return 0;
}

void smm_get_efi_variable_service(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&state;
	*size = sizeof(state);
}

void smm_get_efi_variable_service_buffer(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)transport;
	*size = sizeof(transport);
}

bool smm_efi_variable_service_is_s3_resume(void)
{
	return resume;
}

int smmstore_lookup_region(struct region_device *rdev)
{
	return rdev_chain(rdev, &flash_device, 0, sizeof(flash));
}

uint64_t ass_native_smm_open(void *arena, size_t size, const struct region_device *region,
	uint32_t block, size_t maximum, bool (*enabled)(void), bool secure_boot,
	const struct authvar_fixed_policy *fixed, bool *restart)
{
	assert_int_equal((uintptr_t)arena, (uintptr_t)&state + EFI_VARIABLE_SERVICE_STATE_AREA_SIZE);
	assert_int_equal(region_device_sz(region), sizeof(flash));
	assert_true(enabled());
	starts++;
	*restart = false;
	return owner_failure;
}

bool ass_native_smm_resume(void *arena, size_t size, const struct region_device *region,
	uint32_t block, bool (*enabled)(void), const struct authvar_fixed_policy *fixed)
{
	resumes++;
	return !owner_failure;
}

bool ass_native_smm_boot_access(void *arena, bool *allowed)
{
	*allowed = boot_access;
	return arena && query_fault == QUERY_VALID;
}

#if CONFIG(VBOOT_SLOTS_RW_AB)
const struct ass_capsule_platform *ass_native_smm_capsule_platform(
	const struct efi_variable_service_state *handoff, ass_capsule_program_fn *program,
	uint32_t slot, uint32_t (*commit)(uint32_t))
{
	static struct ass_capsule_platform platform;
	platform.boot_slot = slot;
	platform.commit = commit;
	return &platform;
}

int efi_variable_service_program_capsule(const struct region_device *flash, uint32_t offset,
	const struct efi_capsule_range *ranges, size_t count, uint8_t *image,
	uint8_t *readback, bool check_only)
{
	return -1;
}

struct ass_capsule_transaction *ass_native_smm_capsule_transaction(void *arena,
	const struct ass_capsule_platform *platform, bool resume)
{
	static struct ass_capsule_transaction transaction;
	return &transaction;
}

struct payload_mm_crypto_owner *ass_native_smm_crypto(void *arena)
{
	return NULL;
}
#endif

bool ass_native_smm_variable(void *arena, void *buffer, size_t size)
{
	struct smm_variable_header32 *variable = buffer;
	struct smm_variable_access32 *access = (void *)variable->data;
	const char *name = "bios_lock";
	size_t name_size = (strlen(name) + 1) * sizeof(CHAR16);

	assert_int_equal(variable->function, SMM_VARIABLE_FUNCTION_GET_VARIABLE);
	assert_int_equal(access->data_size, sizeof(cached_value));
	assert_int_equal(access->name_size, name_size);
	assert_int_equal(size, sizeof(*variable) + sizeof(*access) + name_size +
			 sizeof(cached_value));
	assert_memory_equal(&access->guid, &efi_coreboot_nv_data_guid, sizeof(access->guid));
	for (size_t i = 0; i <= strlen(name); i++)
		assert_int_equal(access->name[i], name[i]);
	gets++;
	variable->return_status = cached_status;
	access->attributes = cached_attributes;
	access->data_size = cached_size;
	if (!cached_status && cached_size <= sizeof(cached_value))
		memcpy((uint8_t *)access->name + name_size, &cached_value, cached_size);
	switch (reply_fault) {
	case REPLY_FUNCTION:
		variable->function = 0;
		break;
	case REPLY_NAME_SIZE:
		access->name_size++;
		break;
	case REPLY_DATA_SIZE:
		access->data_size++;
		break;
	case REPLY_GUID:
		access->guid.Data1++;
		break;
	case REPLY_NAME:
		access->name[0]++;
		break;
	case REPLY_VALID:
		break;
	}
	return !native_result;
}

static int setup(void **unused)
{
	memset(flash, 0xff, sizeof(flash));
	memset(&state, 0, sizeof(state));
	memset(transport, 0, sizeof(transport));
	region_device_init(&flash_device, &flash_ops, 0, sizeof(flash));
	current_state = SERVICE_NOT_INITIALIZED;
	service = NULL;
	native_arena = NULL;
	owner_failure = false;
	writer_ready = true;
	resumes = 0;
	resume = false;
	read_error = write_error = readback_error = false;
	writes = starts = 0;
	reads = gets = 0;
	boot_access = true;
	cached_value = 0x12345678;
	cached_size = sizeof(cached_value);
	cached_status = native_result = 0;
	cached_attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
		EFI_VARIABLE_RUNTIME_ACCESS;
	reply_fault = REPLY_VALID;
	query_fault = QUERY_VALID;
	state.signature = EFI_VARIABLE_SERVICE_STATE_SIGNATURE;
	state.revision = EFI_VARIABLE_SERVICE_STATE_REVISION;
	state.size = sizeof(state);
	state.store_size = sizeof(flash);
	state.store_block_size = CONFIG_SMMSTORE_BLOCK_SIZE;
	state.mailbox = (uintptr_t)transport;
	return 0;
}

static void finalize_once(void **unused)
{
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(starts, 1);
}

static void reject_changed_bindings(void **unused)
{
	state.mailbox++;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 0);
	setup(NULL);
	state.store_offset++;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 0);
	setup(NULL);
	writer_ready = false;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 0);
}

static void refuse_failed_owner(void **unused)
{
	owner_failure = true;
	assert_int_equal(efi_variable_service_finalize(), -1);
	owner_failure = false;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 1);
}

static void resume_does_not_initialize(void **unused)
{
	resume = true;
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(resumes, 1);
	assert_int_equal(starts, 0);
	assert_int_equal(writes, 0);
}

#if CONFIG_SMMSTORE_BLOCK_SIZE == 0x1000
static void legacy_store_migrates(void **unused)
{
	bool authenticated;

	assert_int_equal(efi_fv_initialize(&flash_device, 0x10000), CB_SUCCESS);
	state.store_block_size = 0x10000;
	assert_int_equal(efi_variable_service_finalize(), EFI_VARIABLE_SERVICE_REBOOT_REQUIRED);
	assert_int_equal(starts, 1);

	assert_int_equal(current_state, SERVICE_FAILED);
	assert_int_equal(efi_fv_validate_store(&flash_device, 0x1000, &authenticated), CB_SUCCESS);
	assert_true(authenticated);
	/* A fresh stage uses the new HOB geometry; no second conversion. */
	current_state = SERVICE_NOT_INITIALIZED;
	state.store_block_size = 0x1000;
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(starts, 2);
}

static void legacy_geometry_on_s3_is_refused(void **unused)
{
	assert_int_equal(efi_fv_initialize(&flash_device, 0x10000), CB_SUCCESS);
	state.store_block_size = 0x10000;
	resume = true;
	writes = 0;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 0);
	assert_int_equal(writes, 0);
}
#endif

#if CONFIG(VBOOT_SLOTS_RW_AB)
static void acknowledge_boot(void **unused)
{
	struct vb2_context ctx = { 0 };

	ctx.nvdata[0] = VB2_NV_HEADER_SIGNATURE_V1;
	vb2_nv_set(&ctx, VB2_NV_FIRMWARE_SETTINGS_RESET, 1);
	vb2_nv_set(&ctx, VB2_NV_FW_TRIED, 0);
	vb2_nv_set(&ctx, VB2_NV_FW_RESULT, VB2_FW_RESULT_TRYING);
	vb2_nv_set(&ctx, VB2_NV_TRY_COUNT, 2);
	memcpy(nvdata, ctx.nvdata, sizeof(nvdata));
	service = &state;
	state.boot_slot = 1;
	nv_writes = 0;
	assert_int_equal(capsule_commit(0), EFI_VARIABLE_SERVICE_RET_SUCCESS);
	memcpy(ctx.nvdata, nvdata, sizeof(nvdata));
	assert_int_equal(vb2_nv_get(&ctx, VB2_NV_FW_RESULT), VB2_FW_RESULT_SUCCESS);
	assert_int_equal(vb2_nv_get(&ctx, VB2_NV_TRY_COUNT), 0);
	assert_int_equal(nv_writes, 1);

	/* A later handoff must not cancel an update to the other slot. */
	assert_int_equal(capsule_commit(2), EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(capsule_commit(0), EFI_VARIABLE_SERVICE_RET_SUCCESS);
	memcpy(ctx.nvdata, nvdata, sizeof(nvdata));
	assert_int_equal(vb2_nv_get(&ctx, VB2_NV_TRY_NEXT), 1);
	assert_int_equal(vb2_nv_get(&ctx, VB2_NV_TRY_COUNT), 3);
	assert_int_equal(capsule_commit(1), EFI_VARIABLE_SERVICE_RET_FAILURE);

	/* A handoff cannot acknowledge a different slot or malformed state. */
	service = &state;
	state.boot_slot = 2;
	assert_int_equal(capsule_commit(0), EFI_VARIABLE_SERVICE_RET_FAILURE);
	service = &state;
	state.boot_slot = 0;
	assert_int_equal(capsule_commit(0), EFI_VARIABLE_SERVICE_RET_FAILURE);
	service = &state;
	state.boot_slot = 1;
	nvdata[0] ^= 1;
	assert_int_equal(capsule_commit(0), EFI_VARIABLE_SERVICE_RET_FAILURE);
}
#endif
static void prepare_flash_option(const char *name, uint32_t value)
{
	struct region_device rdev;

	assert_int_equal(smmstore_lookup_region(&rdev), 0);
	assert_int_equal(efi_fv_initialize(&rdev, CONFIG_SMMSTORE_BLOCK_SIZE), 0);
	assert_int_equal(efi_fv_set_option(&rdev, &efi_coreboot_nv_data_guid,
					 name, &value, sizeof(value)), 0);
}

static void prepare_cached_option(void)
{
	prepare_flash_option("bios_lock", 0x12345678);
	assert_int_equal(efi_variable_service_finalize(), 0);
}

static void cached_option_read(void **unused)
{
	const uint32_t values[] = { 0, 1, 0x12345678, UINT32_MAX };
	uint32_t value = 0;

	prepare_cached_option();
	read_error = true;
	unsigned int before = reads;
	for (size_t i = 0; i < ARRAY_SIZE(values); i++) {
		cached_value = values[i];
		assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_SUCCESS);
		assert_int_equal(value, cached_value);
		assert_int_equal(gets, i + 1);
		assert_int_equal(reads, before);
	}
}

static void cached_option_errors(void **unused)
{
	const uint32_t sentinel = 0xfeedbeef;
	uint32_t value = sentinel;

	prepare_cached_option();
	read_error = true;
	unsigned int before = reads;
	cached_status = ASS_WIRE_NOT_FOUND;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_OPTION_NOT_FOUND);
	assert_int_equal(value, sentinel);
	assert_int_equal(get_uint_option_status("bios_lock", 7, &value), CB_EFI_OPTION_NOT_FOUND);
	assert_int_equal(value, 7);
	value = sentinel;
	cached_status = ASS_WIRE_BUFFER_TOO_SMALL;
	cached_size = sizeof(value) + 1;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_BUFFER_TOO_SMALL);
	cached_size = sizeof(value);
	cached_status = 0x80000007U;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
	cached_status = 0;
	native_result = 0x80000007U;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
	native_result = 0;
	for (reply_fault = REPLY_FUNCTION; reply_fault <= REPLY_NAME; reply_fault++) {
		assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
		assert_int_equal(value, sentinel);
	}
	reply_fault = REPLY_VALID;
	native_result = 1;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
	assert_int_equal(efi_variable_service_get_uint(NULL, &value), CB_ERR_ARG);
	assert_int_equal(efi_variable_service_get_uint("bios_lock", NULL), CB_ERR_ARG);
	assert_int_equal(value, sentinel);
	assert_int_equal(reads, before);
}

static void cached_lifecycle_errors(void **unused)
{
	uint32_t value = 0xfeedbeef;

	prepare_cached_option();
	read_error = true;
	unsigned int before = reads;
	for (query_fault = QUERY_ERROR; query_fault <= QUERY_VALUE; query_fault++) {
		assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
		assert_false(efi_variable_service_boot_access_allowed());
		assert_int_equal(value, 0xfeedbeef);
		assert_int_equal(gets, 0);
		assert_int_equal(reads, before);
	}
}

static void legacy_option_without_owner(void **unused)
{
	uint32_t value = 42, result;

	prepare_flash_option("bios_lock", value);
	for (unsigned int i = 0; i < 2; i++) {
		current_state = i ? SERVICE_FAILED : SERVICE_NOT_INITIALIZED;
		assert_int_equal(efi_option_get_uint("bios_lock", &result), CB_SUCCESS);
		assert_int_equal(result, value);
	}
	assert_int_equal(gets, 0);
	assert_int_equal(starts, 0);
}

static void legacy_option_after_boot(void **unused)
{
	uint32_t value = 0;

	prepare_cached_option();
	EFI_FIRMWARE_VOLUME_HEADER *fv = (void *)flash;
	AUTHENTICATED_VARIABLE_HEADER *var =
		(void *)(flash + fv->HeaderLength + sizeof(VARIABLE_STORE_HEADER));
	assert_int_equal(var->StartId, VARIABLE_DATA);
	var->Attributes = EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS;
	boot_access = false;
	assert_false(efi_variable_service_boot_access_allowed());
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_SUCCESS);
	assert_int_equal(value, 0x12345678);
	assert_int_equal(gets, 0);
}

static void long_option_name(void **unused)
{
	char name[128];
	uint32_t value = 42, result;

	memset(name, 'a', sizeof(name) - 1);
	name[sizeof(name) - 1] = 0;
	prepare_flash_option(name, value);
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(efi_option_get_uint(name, &result), CB_SUCCESS);
	assert_int_equal(result, value);
	assert_int_equal(gets, 0);
}

static void volatile_option_does_not_shadow_flash(void **unused)
{
	const struct { uint32_t size, status; } cases[] = {
		{ 1, EFI_SUCCESS }, { 4, EFI_SUCCESS }, { 8, ASS_WIRE_BUFFER_TOO_SMALL },
	};
	uint32_t value = 0;

	prepare_cached_option();
	cached_value = 0;
	cached_attributes &= ~EFI_VARIABLE_NON_VOLATILE;
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		unsigned int before = reads;

		cached_size = cases[i].size;
		cached_status = cases[i].status;
		assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_SUCCESS);
		assert_int_equal(value, 0x12345678);
		assert_int_equal(gets, i + 1);
		assert_true(reads > before);
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
#if CONFIG(VBOOT_SLOTS_RW_AB)
		cmocka_unit_test_setup(acknowledge_boot, setup),
#endif
		cmocka_unit_test_setup(finalize_once, setup),
		cmocka_unit_test_setup(reject_changed_bindings, setup),
		cmocka_unit_test_setup(refuse_failed_owner, setup),
		cmocka_unit_test_setup(resume_does_not_initialize, setup),
		cmocka_unit_test_setup(cached_option_read, setup),
		cmocka_unit_test_setup(cached_option_errors, setup),
		cmocka_unit_test_setup(cached_lifecycle_errors, setup),
		cmocka_unit_test_setup(legacy_option_without_owner, setup),
		cmocka_unit_test_setup(legacy_option_after_boot, setup),
		cmocka_unit_test_setup(long_option_name, setup),
		cmocka_unit_test_setup(volatile_option_does_not_shadow_flash, setup),
#if CONFIG_SMMSTORE_BLOCK_SIZE == 0x1000
		cmocka_unit_test_setup(legacy_store_migrates, setup),
		cmocka_unit_test_setup(legacy_geometry_on_s3_is_refused, setup),
#endif
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
