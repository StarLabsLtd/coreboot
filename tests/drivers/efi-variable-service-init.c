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

static uint32_t read_spi_bar(pci_devfn_t dev, uint16_t reg)
{
	assert_int_equal(reg, PCI_BASE_ADDRESS_0);
	return 0xfe010000;
}

#undef pci_read_config32
#define pci_read_config32 read_spi_bar
/* The mocked IA32 MM entry points use the host C calling convention. */
#if defined(__x86_64__)
#define cdecl sysv_abi
#endif
#include <drivers/efi_variable_service/smi.c>
#undef cdecl

static uint8_t flash[0x80000];
static uint8_t transport[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
static struct region_device flash_device;
static struct efi_variable_service_state state;
static struct {
	struct service_info_hob info;
	struct hob_resource spi;
#if CONFIG(VBOOT_SLOTS_RW_AB)
	struct capsule_vboot_hob capsule;
#endif
} hobs;
static bool resume;
static bool read_error, write_error, readback_error;
static unsigned int writes, starts;
static unsigned int reads, gets;
static uint32_t validation_at_start;
static bool boot_access;
static uint32_t cached_value, cached_size, cached_status, cached_attributes, native_result;
static enum {
	REPLY_VALID, REPLY_SHORT, REPLY_LONG, REPLY_FUNCTION,
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

int boot_device_rw_subregion(const struct region *region, struct region_device *out)
{
	return rdev_chain(out, &flash_device, region_offset(region), region_sz(region));
}

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

bool fast_spi_wpd_status(void)
{
	return true;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = 0x70000000;
	*size = 0x1000000;
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

static uint32_t mm_manage(const EFI_GUID *guid, const void *context,
			 void *buffer, size_t *size)
{
	struct smm_variable_header32 *variable = buffer;
	struct smm_variable_access32 *access = (void *)variable->data;
	const char *name = "bios_lock";
	size_t name_size = (strlen(name) + 1) * sizeof(CHAR16);

	if (!memcmp(guid, &efi_variable_service_ready_guid, sizeof(*guid))) {
		assert_int_equal(*size, 1);
		*(uint8_t *)buffer = boot_access;
		if (query_fault == QUERY_ERROR)
			return EFI_DEVICE_ERROR;
		if (query_fault == QUERY_SIZE)
			*size = 0;
		if (query_fault == QUERY_VALUE)
			*(uint8_t *)buffer = 2;
		return 0;
	}
	assert_memory_equal(guid, &efi_smm_variable_protocol_guid, sizeof(*guid));
	assert_int_equal(variable->function, SMM_VARIABLE_FUNCTION_GET_VARIABLE);
	assert_int_equal(access->data_size, sizeof(cached_value));
	assert_int_equal(access->name_size, name_size);
	assert_int_equal(*size, sizeof(*variable) + sizeof(*access) + name_size +
			 sizeof(cached_value));
	assert_memory_equal(&access->guid, &efi_coreboot_nv_data_guid, sizeof(*guid));
	for (size_t i = 0; i <= strlen(name); i++)
		assert_int_equal(access->name[i], name[i]);
	gets++;
	variable->return_status = cached_status;
	access->attributes = cached_attributes;
	access->data_size = cached_size;
	if (!cached_status && cached_size <= sizeof(cached_value))
		memcpy((uint8_t *)access->name + name_size, &cached_value, cached_size);
	switch (reply_fault) {
	case REPLY_SHORT:
		*size = sizeof(*variable) - 1;
		break;
	case REPLY_LONG:
		*size = SIZE_MAX;
		break;
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
	return native_result;
}

/* The external MM implementation is a dependency boundary, not a second parser. */
static void mm_dispatch(const struct mm_entry_context *context)
{
	struct efi_mm_communication_status *status =
		(void *)(transport + sizeof(struct efi_variable_service_buffer));

#if CONFIG(VBOOT_SLOTS_RW_AB)
	struct mm_communication_header32 *message =
		(void *)(transport + EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE);

	if (message->message_length == 24) {
		uint32_t *geometry = (void *)message->data;

		geometry[3] = CONFIG_ROM_SIZE;
		geometry[4] = EFI_CAPSULE_BLOCK_SIZE;
	}
#endif
	status->valid = 0;
}

static void mm_start(const void *hob_base)
{
	starts++;
	validation_at_start = private_validate();
	if (validation_at_start == EFI_VARIABLE_SERVICE_RET_SUCCESS)
		assert_int_equal(register_mm_entry((uintptr_t)mm_dispatch,
						  (uintptr_t)mm_manage), 0);
}

static int setup(void **unused)
{
	memset(flash, 0xff, sizeof(flash));
	memset(&hobs, 0, sizeof(hobs));
	memset(&state, 0, sizeof(state));
	memset(transport, 0, sizeof(transport));
	region_device_init(&flash_device, &flash_ops, 0, sizeof(flash));
	current_state = SERVICE_NOT_INITIALIZED;
	service = NULL;
	sealed_spi_bar = 0;
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
	validation_at_start = UINT32_MAX;
	state.signature = EFI_VARIABLE_SERVICE_STATE_SIGNATURE;
	state.revision = EFI_VARIABLE_SERVICE_STATE_REVISION;
	state.size = sizeof(state);
	state.core_base = MIN(MIN((uintptr_t)mm_start, (uintptr_t)mm_dispatch),
			      (uintptr_t)mm_manage);
	state.core_size = MAX(MAX((uintptr_t)mm_start, (uintptr_t)mm_dispatch),
			      (uintptr_t)mm_manage) -
		state.core_base + 1;
	state.core_entry = (uintptr_t)mm_start;
	state.hob_base = (uintptr_t)&hobs;
	state.hob_size = sizeof(hobs);
	state.service_info = (uintptr_t)&hobs.info;
#if CONFIG(VBOOT_SLOTS_RW_AB)
	state.capsule_info = (uintptr_t)&hobs.capsule;
	hobs.capsule.base = (struct capsule_info_hob) {
		.revision = 2, .size = sizeof(hobs.capsule), .flash_size = CONFIG_ROM_SIZE,
		.block_size = EFI_CAPSULE_BLOCK_SIZE,
	};
	hobs.capsule.boot_slot = 2;
	capsule_boot_slot = 0;
#endif
	state.spi_mmio_resource = (uintptr_t)&hobs.spi;
	state.store_size = sizeof(flash);
	state.store_block_size = CONFIG_SMMSTORE_BLOCK_SIZE;
	hobs.info.revision = EFI_VARIABLE_SERVICE_INFO_REVISION;
	hobs.info.size = sizeof(hobs.info);
	hobs.info.buffer = (uintptr_t)transport;
	hobs.info.buffer_size = sizeof(transport);
	hobs.spi.header.type = HOB_RESOURCE_DESCRIPTOR;
	hobs.spi.header.length = sizeof(hobs.spi);
	hobs.spi.type = RESOURCE_MEMORY_MAPPED_IO;
	hobs.spi.size = 0x1000;
	return 0;
}

static void erased_first_boot(void **unused)
{
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(starts, 1);
	assert_int_equal(validation_at_start, EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(writes, 1);
	/* Initialization writes headers, leaving FTW to MM. */
	for (size_t i = 0x30000; i < sizeof(flash); i++)
		assert_int_equal(flash[i], 0xff);
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(writes, 1);
}

static void interrupted_header(void **unused)
{
	assert_int_equal(efi_fv_initialize(&flash_device, CONFIG_SMMSTORE_BLOCK_SIZE), 0);
	/* Preserve the beginning of a header write, not a populated store. */
	memset(flash + 32, 0xff, sizeof(flash) - 32);
	writes = 0;
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(starts, 1);
	assert_int_equal(validation_at_start, EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(writes, 1);
}

static void incompatible_header(void **unused)
{
	flash[0] = 0;
	flash[128] = 0;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 1);
	assert_int_equal(writes, 0);
	assert_int_equal(flash[128], 0);
}

static void existing_store(void **unused)
{
	assert_int_equal(efi_fv_initialize(&flash_device, CONFIG_SMMSTORE_BLOCK_SIZE), 0);
	/* A non-erased FTW area must not be touched by provisioning. */
	flash[0x30000] = 0x5a;
	writes = 0;
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(starts, 1);
	assert_int_equal(writes, 0);
	assert_int_equal(flash[0x30000], 0x5a);
}

static void invalid_primary_reaches_mm(void **unused)
{
	flash[0x30000] = 0x5a;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 1);
	assert_int_equal(validation_at_start, EFI_VARIABLE_SERVICE_RET_FAILURE);
	assert_int_equal(writes, 0);
	assert_int_equal(flash[0x30000], 0x5a);
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 1);
}

static void initialization_io_failure(void **unused)
{
	read_error = true;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 0);
	assert_int_equal(writes, 0);
	setup(NULL);
	write_error = true;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 0);
	assert_int_equal(writes, 1);
	setup(NULL);
	readback_error = true;
	assert_int_equal(efi_variable_service_finalize(), -1);
	assert_int_equal(starts, 0);
	assert_int_equal(writes, 1);
}

static void resume_does_not_initialize(void **unused)
{
	assert_int_equal(efi_variable_service_finalize(), 0);
	current_state = SERVICE_NOT_INITIALIZED;
#if CONFIG(VBOOT_SLOTS_RW_AB)
	/* Native SMM BSS is reset while the private MM HOB remains in SMRAM. */
	capsule_boot_slot = 0;
#endif
	resume = true;
	memset(flash, 0xff, sizeof(flash));
	writes = starts = 0;
	/* The external MM state is retained; no primary initialization on S3. */
	assert_int_equal(efi_variable_service_finalize(), 0);
	assert_int_equal(writes, 0);
	assert_int_equal(starts, 0);
}

#if CONFIG_SMMSTORE_BLOCK_SIZE == 0x1000
static void legacy_store_migrates(void **unused)
{
	bool authenticated;

	assert_int_equal(efi_fv_initialize(&flash_device, 0x10000), CB_SUCCESS);
	state.store_block_size = 0x10000;
	assert_int_equal(efi_variable_service_finalize(), EFI_VARIABLE_SERVICE_REBOOT_REQUIRED);
	assert_int_equal(starts, 1);
	assert_int_equal(validation_at_start, EFI_VARIABLE_SERVICE_RET_SUCCESS);
	assert_int_equal(current_state, SERVICE_FAILED);
	assert_int_equal(efi_fv_validate_store(&flash_device, 0x1000, &authenticated), CB_SUCCESS);
	assert_true(authenticated);
	/* A fresh stage uses the new HOB geometry; no second conversion. */
	current_state = SERVICE_NOT_INITIALIZED;
	state.store_block_size = 0x1000;
	state.dispatch_entry = state.manage_entry = 0;
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
	capsule_boot_slot = 1;
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
	capsule_boot_slot = 2;
	assert_int_equal(capsule_commit(0), EFI_VARIABLE_SERVICE_RET_FAILURE);
	capsule_boot_slot = 0;
	assert_int_equal(capsule_commit(0), EFI_VARIABLE_SERVICE_RET_FAILURE);
	capsule_boot_slot = 1;
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
	cached_status = EFI_NOT_FOUND;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_OPTION_NOT_FOUND);
	assert_int_equal(value, sentinel);
	assert_int_equal(get_uint_option_status("bios_lock", 7, &value), CB_EFI_OPTION_NOT_FOUND);
	assert_int_equal(value, 7);
	value = sentinel;
	cached_status = EFI_BUFFER_TOO_SMALL;
	cached_size = sizeof(value) + 1;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_BUFFER_TOO_SMALL);
	cached_size = sizeof(value);
	cached_status = EFI_DEVICE_ERROR;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
	cached_status = 0;
	native_result = EFI_DEVICE_ERROR;
	assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
	native_result = 0;
	for (reply_fault = REPLY_SHORT; reply_fault <= REPLY_NAME; reply_fault++) {
		assert_int_equal(efi_option_get_uint("bios_lock", &value), CB_EFI_ACCESS_ERROR);
		assert_int_equal(value, sentinel);
	}
	reply_fault = REPLY_VALID;
	state.manage_entry = 0;
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
		{ 1, EFI_SUCCESS }, { 4, EFI_SUCCESS }, { 8, EFI_BUFFER_TOO_SMALL },
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
		cmocka_unit_test_setup(erased_first_boot, setup),
		cmocka_unit_test_setup(existing_store, setup),
		cmocka_unit_test_setup(interrupted_header, setup),
		cmocka_unit_test_setup(incompatible_header, setup),
		cmocka_unit_test_setup(invalid_primary_reaches_mm, setup),
		cmocka_unit_test_setup(initialization_io_failure, setup),
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
