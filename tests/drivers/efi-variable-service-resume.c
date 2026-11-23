/* SPDX-License-Identifier: GPL-2.0-only */

#include <tests/test.h>
#include <drivers/efi_variable_service/ramstage.c>

static uint8_t smram[0x400000] __aligned(4096);
static uint8_t transport[EFI_VARIABLE_SERVICE_BUFFER_SIZE];
static bool resume;

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

uintptr_t cbmem_top(void)
{
	return 0x4000000;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = 0x5000000;
	*size = sizeof(smram);
}

int smmstore_get_info(struct smmstore_params_info *info)
{
	info->num_blocks = 8;
	info->block_size = 0x10000;
	return 0;
}

int fmap_locate_area(const char *name, struct region *store)
{
	assert_string_equal(name, "SMMSTORE");
	*store = region_create(0x80000, 0x80000);
	return 0;
}

static int setup(void **unused)
{
	struct smm_runtime runtime = { 0 };

	resume = false;
	memset(smram, 0, sizeof(smram));
	efi_variable_service_load(&runtime);
	assert_true(service_prepared);
	resume = true;
	return 0;
}

static void resume_preserves_owner(void **unused)
{
	struct smm_runtime runtime = { 0 };

	smram[EFI_VARIABLE_SERVICE_STATE_AREA_SIZE] = 0x5a;
	efi_variable_service_load(&runtime);
	assert_true(service_prepared);
	assert_true(runtime.efi_variable_service_s3_resume);
	assert_int_equal(smram[EFI_VARIABLE_SERVICE_STATE_AREA_SIZE], 0x5a);
}

static void reject_changed_mailbox(void **unused)
{
	struct smm_runtime runtime = { 0 };
	struct efi_variable_service_state *state = (void *)smram;

	state->mailbox++;
	efi_variable_service_load(&runtime);
	assert_false(service_prepared);
	assert_int_equal(runtime.efi_variable_service_base, 0);
}

static void reject_changed_ram(void **unused)
{
	struct smm_runtime runtime = { 0 };
	struct efi_variable_service_state *state = (void *)smram;

	state->capsule_ram_end--;
	efi_variable_service_load(&runtime);
	assert_false(service_prepared);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(resume_preserves_owner, setup),
		cmocka_unit_test_setup(reject_changed_mailbox, setup),
		cmocka_unit_test_setup(reject_changed_ram, setup),
	};
	return cmocka_run_group_tests(tests, NULL, NULL);
}
