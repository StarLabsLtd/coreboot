/* SPDX-License-Identifier: GPL-2.0-only */

#include "../drivers/payload_mm_interface/smi.c"
#include <tests/test.h>

static uint8_t reserved_region[8192];
static size_t reserved_size;
static bool overlaps_smram;
static bool resume;

bool smm_is_s3_resume(void)
{
	return resume;
}

void payload_mm_get_reserved_region(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)reserved_region;
	*size = reserved_size;
}

bool smm_region_overlaps_handler(const struct region *region)
{
	return overlaps_smram;
}

static int setup(void **state)
{
	memset(reserved_region, 0, sizeof(reserved_region));
	reserved_size = sizeof(reserved_region);
	overlaps_smram = false;
	load_attempted = false;
	registered = false;
	initialized = false;
	resume = false;
	return 0;
}

static void cold_dispatch_keeps_loader_available(void **state)
{
	memset(reserved_region, 0xa5, sizeof(reserved_region));
	payload_mm_call_entrypoint();
	assert_true(initialized);
	assert_false(load_attempted);
	assert_false(registered);
	for (size_t i = 0; i < sizeof(reserved_region); i++)
		assert_int_equal(reserved_region[i],
				 i < PLD_MM_SHARED_MEMORY_MAX_SIZE ? 0 : 0xa5);
}

static void cold_close_discards_stale_registration(void **state)
{
	struct payload_mm_shared_info *info = (void *)reserved_region;
	*info = (struct payload_mm_shared_info) {
		.header_magic = PLD_MM_SHARED_STRUCT_MAGIC,
		.shared_info_size = sizeof(*info),
		.header_revision = PLD_MM_SHARED_STRUCT_REVISION,
		.mm_entrypoint_address = (uintptr_t)reserved_region + PLD_MM_SHARED_MEMORY_MAX_SIZE,
	};
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_CLOSE_LOADER, NULL),
			 PAYLOAD_MM_RET_FAILURE);
	assert_int_equal(info->header_magic, 0);
	initialized = false;
	load_attempted = false;
	resume = true;
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_CLOSE_LOADER, NULL),
			 PAYLOAD_MM_RET_FAILURE);
	assert_true(load_attempted);
	assert_false(registered);
}

static void resume_restores_registration(void **state)
{
	struct payload_mm_shared_info *info = (void *)reserved_region;
	const uintptr_t base = (uintptr_t)reserved_region;
	assert_true(base + sizeof(reserved_region) <= UINT32_MAX);
	*info = (struct payload_mm_shared_info) {
		.header_magic = PLD_MM_SHARED_STRUCT_MAGIC,
		.shared_info_size = sizeof(*info),
		.header_revision = PLD_MM_SHARED_STRUCT_REVISION,
		.mm_entrypoint_address = base + PLD_MM_SHARED_MEMORY_MAX_SIZE,
	};
	resume = true;
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_CLOSE_LOADER, NULL),
			 PAYLOAD_MM_RET_SUCCESS);
	assert_true(registered);
	assert_true(load_attempted);
	assert_int_equal(info->header_magic, PLD_MM_SHARED_STRUCT_MAGIC);
	assert_int_equal(info->mm_entrypoint_address, base + PLD_MM_SHARED_MEMORY_MAX_SIZE);
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE, NULL),
			 PAYLOAD_MM_RET_FAILURE);
}

static void resume_without_registration_keeps_loader_closed(void **state)
{
	resume = true;
	payload_mm_call_entrypoint();
	assert_true(initialized);
	assert_true(load_attempted);
	assert_false(registered);
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE, NULL),
			 PAYLOAD_MM_RET_FAILURE);
}

static void resume_invalid_registration_keeps_loader_closed(void **state)
{
	struct payload_mm_shared_info *info = (void *)reserved_region;
	info->header_magic = PLD_MM_SHARED_STRUCT_MAGIC;
	info->shared_info_size = sizeof(*info);
	info->header_revision = PLD_MM_SHARED_STRUCT_REVISION;
	info->mm_entrypoint_address = (uintptr_t)reserved_region;
	resume = true;
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE, NULL),
			 PAYLOAD_MM_RET_FAILURE);
	assert_true(load_attempted);
	assert_false(registered);
}

static void reject_missing_context(void **state)
{
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE, NULL),
			 PAYLOAD_MM_RET_FAILURE);
	assert_true(load_attempted);
	assert_false(registered);
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE, NULL),
			 PAYLOAD_MM_RET_FAILURE);
}

static void reject_unknown_command(void **state)
{
	assert_int_equal(payload_mm_exec_interface(0, NULL), PAYLOAD_MM_RET_FAILURE);
	assert_true(load_attempted);
	assert_false(registered);
}

static void close_unused_loader(void **state)
{
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_CLOSE_LOADER, NULL),
			 PAYLOAD_MM_RET_FAILURE);
	assert_true(load_attempted);
	assert_false(registered);
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE, NULL),
			 PAYLOAD_MM_RET_FAILURE);
}

static void close_registered_loader(void **state)
{
	/* Model a completed registration without executing a loaded image. */
	initialized = true;
	registered = true;
	load_attempted = true;
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_CLOSE_LOADER, NULL),
			 PAYLOAD_MM_RET_SUCCESS);
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_CLOSE_LOADER, NULL),
			 PAYLOAD_MM_RET_SUCCESS);
	assert_int_equal(payload_mm_exec_interface(PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE, NULL),
			 PAYLOAD_MM_RET_FAILURE);
}

static void reject_context_in_smram(void **state)
{
	struct payload_mm_load_context context = { 0 };
	overlaps_smram = true;
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
}

static void reject_context_version(void **state)
{
	struct payload_mm_load_context context = {
		.header_size = sizeof(context),
		.header_revision = PLD_MM_LOAD_CONTEXT_REVISION + 1,
	};
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	context.header_revision = PLD_MM_LOAD_CONTEXT_REVISION;
	context.reserved = 1;
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
}

static void validate_registered_header(void **state)
{
	struct payload_mm_shared_info *info = (void *)reserved_region;
	const uintptr_t base = (uintptr_t)reserved_region;
	assert_true(base + sizeof(reserved_region) <= UINT32_MAX);
	info->header_magic = PLD_MM_SHARED_STRUCT_MAGIC;
	info->shared_info_size = sizeof(*info);
	info->header_revision = PLD_MM_SHARED_STRUCT_REVISION;
	info->mm_entrypoint_address = base + PLD_MM_SHARED_MEMORY_MAX_SIZE;
	assert_int_equal(payload_mm_get_entrypoint(), info->mm_entrypoint_address);
	info->mm_entrypoint_address = base + sizeof(reserved_region);
	assert_int_equal(payload_mm_get_entrypoint(), 0);
	info->mm_entrypoint_address = base;
	assert_int_equal(payload_mm_get_entrypoint(), 0);
	info->mm_entrypoint_address = base + PLD_MM_SHARED_MEMORY_MAX_SIZE;
	info->shared_info_size--;
	assert_int_equal(payload_mm_get_entrypoint(), 0);
	info->shared_info_size++;
	info->header_revision++;
	assert_int_equal(payload_mm_get_entrypoint(), 0);
	info->header_revision--;
	info->reserved = 1;
	assert_int_equal(payload_mm_get_entrypoint(), 0);
	info->reserved = 0;
	reserved_size = PLD_MM_SHARED_MEMORY_MAX_SIZE;
	assert_int_equal(payload_mm_get_entrypoint(), 0);
}

static void reject_image_bounds(void **state)
{
	struct payload_mm_load_context context = {
		.header_size = sizeof(context),
		.header_revision = PLD_MM_LOAD_CONTEXT_REVISION,
		.mm_core_source_address = (uintptr_t)reserved_region,
		.mm_core_destination_address = (uintptr_t)reserved_region,
		.mm_core_size = 1,
	};
	/* The destination may not overwrite shared metadata. */
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	context.mm_core_destination_address += PLD_MM_SHARED_MEMORY_MAX_SIZE;
	context.mm_core_size = 0;
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	context.mm_core_size = 2;
	context.mm_core_source_address = UINTPTR_MAX;
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	context.mm_core_source_address = 0;
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	context.mm_core_source_address = (uintptr_t)reserved_region;
	context.mm_core_destination_address = UINT32_MAX;
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	context.mm_core_destination_address = (uintptr_t)reserved_region + sizeof(reserved_region);
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	context.mm_core_destination_address = (uintptr_t)reserved_region + PLD_MM_SHARED_MEMORY_MAX_SIZE;
	context.mm_entrypoint_offset = context.mm_core_size;
	assert_int_equal(payload_mm_load_and_call_core_module(&context), PAYLOAD_MM_RET_FAILURE);
	assert_memory_equal(reserved_region, (uint8_t[sizeof(reserved_region)]) { 0 }, sizeof(reserved_region));
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test_setup(cold_dispatch_keeps_loader_available, setup),
		cmocka_unit_test_setup(cold_close_discards_stale_registration, setup),
		cmocka_unit_test_setup(resume_restores_registration, setup),
		cmocka_unit_test_setup(resume_without_registration_keeps_loader_closed, setup),
		cmocka_unit_test_setup(resume_invalid_registration_keeps_loader_closed, setup),
		cmocka_unit_test_setup(reject_missing_context, setup),
		cmocka_unit_test_setup(reject_unknown_command, setup),
		cmocka_unit_test_setup(close_unused_loader, setup),
		cmocka_unit_test_setup(close_registered_loader, setup),
		cmocka_unit_test_setup(reject_context_in_smram, setup),
		cmocka_unit_test_setup(reject_context_version, setup),
		cmocka_unit_test_setup(validate_registered_header, setup),
		cmocka_unit_test_setup(reject_image_bounds, setup),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
