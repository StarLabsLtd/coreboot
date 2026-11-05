/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <drivers/efi_variable_service/ramstage.c>

static uint8_t smram[0x400000] __aligned(4096);
static uint8_t transport[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
static const uint8_t firmware[0x2000];
static bool resume;
static unsigned int loads;

bool romstage_handoff_is_resume(void)
{
	return resume;
}

int smm_subregion(int sub, uintptr_t *base, size_t *size)
{
	assert_int_equal(sub, SMM_SUBREGION_EFI_VARIABLE_SERVICE);
	*base = (uintptr_t)smram;
	*size = sizeof(smram);
	return 0;
}

void *_cbfs_alloc(const char *name, cbfs_allocator_t allocator, void *arg,
		 size_t *size, bool force_ro, enum cbfs_type *type)
{
	assert_string_equal(name, "efi_variable_service_fv");
	loads++;
	*size = sizeof(firmware);
	void *out = allocator(arg, *size, NULL);
	assert_non_null(out);
	memcpy(out, firmware, *size);
	return out;
}

void *cbmem_add(u32 id, u64 size)
{
	assert_int_equal(id, CBMEM_ID_EFI_VARIABLE_SERVICE);
	assert_int_equal(size, sizeof(transport));
	return transport;
}

const struct cbmem_entry *cbmem_entry_find(u32 id)
{
	assert_int_equal(id, CBMEM_ID_EFI_VARIABLE_SERVICE);
	return (void *)transport;
}

u64 cbmem_entry_size(const struct cbmem_entry *entry)
{
	return sizeof(transport);
}

int efi_variable_service_load_mm_core(const void *fv, size_t size,
		void *destination, size_t available, struct efi_variable_service_image *image)
{
	assert_false(resume);
	image->base = destination;
	image->size = 0x1000;
	image->entry = destination;
	return 0;
}

int efi_variable_service_build_hobs(uintptr_t base, uintptr_t end,
		uintptr_t fv, size_t size, const struct efi_variable_service_image *image,
		uintptr_t buffer, size_t buffer_size, struct efi_variable_service_state *state)
{
	struct service_info_hob *info = (void *)base;
	*info = (struct service_info_hob) {
		.revision = EFI_VARIABLE_SERVICE_INFO_REVISION,
		.buffer = buffer,
		.buffer_size = buffer_size,
	};
	state->hob_base = base;
	state->hob_size = sizeof(*info);
	state->service_info = base;
	state->dispatch_entry = state->core_entry;
	state->manage_entry = state->core_entry;
	return 0;
}

static int setup(void **unused)
{
	struct smm_runtime runtime = {0};

	resume = false;
	loads = 0;
	memset(smram, 0, sizeof(smram));
	efi_variable_service_load(&runtime);
	assert_true(service_prepared);
	assert_int_equal(loads, 1);
	resume = true;
	loads = 0;
	return 0;
}

static void resume_without_loading_fv(void **unused)
{
	struct smm_runtime runtime = {0};

	efi_variable_service_load(&runtime);
	assert_true(service_prepared);
	assert_true(runtime.efi_variable_service_s3_resume);
	assert_int_equal(loads, 0);
	assert_memory_equal(smram + SERVICE_STATE_AREA_SIZE, firmware, sizeof(firmware));
}

static void reject_invalid_retained_size(void **unused)
{
	struct efi_variable_service_state *state = (void *)smram;
	struct smm_runtime runtime = {0};

	state->fv_size = 0;
	efi_variable_service_load(&runtime);
	assert_false(service_prepared);
	assert_int_equal(runtime.efi_variable_service_base, 0);
	assert_int_equal(loads, 0);
	state->fv_size = UINT32_MAX;
	efi_variable_service_load(&runtime);
	assert_false(service_prepared);
	assert_int_equal(runtime.efi_variable_service_base, 0);
	assert_int_equal(loads, 0);
}

static void reject_invalid_retained_entry(void **unused)
{
	struct efi_variable_service_state *state = (void *)smram;
	struct smm_runtime runtime = {0};

	state->dispatch_entry = state->core_base - 1;
	efi_variable_service_load(&runtime);
	assert_false(service_prepared);
	assert_int_equal(runtime.efi_variable_service_base, 0);
	assert_int_equal(loads, 0);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(resume_without_loading_fv, setup),
		cmocka_unit_test_setup(reject_invalid_retained_size, setup),
		cmocka_unit_test_setup(reject_invalid_retained_entry, setup),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
