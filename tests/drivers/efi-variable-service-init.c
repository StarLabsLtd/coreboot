/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <tests/test.h>
#if CONFIG(VBOOT_SLOTS_RW_AB)
#include <2nvstorage_fields.h>
#endif

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
static uint32_t validation_at_start;

static ssize_t flash_read(const struct region_device *rdev, void *buf, size_t offset,
			  size_t size)
{
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
						  (uintptr_t)mm_dispatch), 0);
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
	validation_at_start = UINT32_MAX;
	state.signature = EFI_VARIABLE_SERVICE_STATE_SIGNATURE;
	state.revision = EFI_VARIABLE_SERVICE_STATE_REVISION;
	state.size = sizeof(state);
	state.core_base = MIN((uintptr_t)mm_start, (uintptr_t)mm_dispatch);
	state.core_size = MAX((uintptr_t)mm_start, (uintptr_t)mm_dispatch) -
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
#if CONFIG_SMMSTORE_BLOCK_SIZE == 0x1000
		cmocka_unit_test_setup(legacy_store_migrates, setup),
		cmocka_unit_test_setup(legacy_geometry_on_s3_is_refused, setup),
#endif
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
