/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/lpc.h>
#include <amdblocks/psp.h>
#include <amdblocks/spi.h>
#include <boot_device.h>
#include <cpu/amd/msr.h>
#include <cpu/x86/cache.h>
#include <cpu/x86/smm.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <tests/test.h>
#include <unistd.h>
#include "psp_def.h"
#include "psp_rom_armor_apmc.h"

static int info_transport_status, enter_transport_status, hsti_transport_status;
static int pre_hsti_transport_status;
static uint32_t info_command_status, enter_command_status, hsti_command_status;
static uint32_t pre_hsti_command_status;
static uint32_t flash_size = CONFIG_ROM_SIZE;
static uint32_t hsti_state = HSTI_STATE_ROM_ARMOR_ENFORCED;
static unsigned int info_calls, enter_calls, hsti_calls, write_calls, erase_calls;
static unsigned int fch_burst_calls, cache_flushes;
static bool enter_succeeded, probe_during_enter;
static struct mbox_cmd_smm_info_buffer *info_buffer;
static struct mbox_rom_armor_enforce_buffer *enter_buffer;
static struct mbox_cmd_hsti_query_buffer *hsti_buffer;
static uint8_t mapped_flash[CONFIG_ROM_SIZE];
static struct mbox_rom_armor_flash_command_buffer *spi_buffer;
static int spi_transport_status, sx_transport_status;
static uint32_t spi_command_status;
static unsigned int sx_calls;
static uint8_t expected_write_value = 0x5a;
static const struct region_device *probe_during_spi;

static void assert_persistent_buffer(const struct mbox_buffer_header *header, size_t size)
{
	extern char __data_start[], _end[];

	assert_int_equal(header->size, size);
	assert_true(IS_ALIGNED((uintptr_t)header, 32));
	if (CONFIG(ASS)) {
		assert_true((uintptr_t)header >= (uintptr_t)__data_start);
		assert_true((uintptr_t)header <= (uintptr_t)_end - size);
	}
}

int send_psp_command(uint32_t command, void *buffer)
{
	switch (command) {
	case MBOX_BIOS_CMD_SMM_INFO: {
		struct mbox_cmd_smm_info_buffer *info = buffer;

		info_calls++;
		info_buffer = info;
		assert_persistent_buffer(&info->header, sizeof(*info));
		assert_int_equal(info->req.smm_base, 0x400000);
		assert_int_equal(info->req.smm_mask, 0xffe00000);
		assert_int_equal(info->req.smm_trig_info.address, 0x1234);
		assert_int_equal(info->req.smm_reg_info.eos.address, 0x5678);
		assert_non_null((void *)(uintptr_t)info->req.psp_mbox_smm_buffer_address);
		assert_non_null((void *)(uintptr_t)info->req.psp_mbox_smm_flag_address);
		info->header.status = info_command_status;
		return info_transport_status;
	}
	case MBOX_BIOS_CMD_ARMOR_ENTER_SMM_MODE: {
		struct mbox_rom_armor_enforce_buffer *enter = buffer;

		enter_calls++;
		enter_buffer = enter;
		assert_persistent_buffer(&enter->header, sizeof(*enter));
		if (probe_during_enter) {
			const unsigned int calls = fch_burst_calls;

			expect_assert_failure(boot_device_ro());
			assert_int_equal(fch_burst_calls, calls);
		}
		enter->header.status = enter_command_status;
		enter->flash_size = flash_size;
		enter_succeeded = !enter_transport_status && !enter_command_status;
		return enter_transport_status;
	}
	case MBOX_BIOS_CMD_HSTI_QUERY: {
		struct mbox_cmd_hsti_query_buffer *hsti = buffer;

		hsti_calls++;
		assert_persistent_buffer(&hsti->header, sizeof(*hsti));
		assert_int_equal(hsti->header.status, 0);
		hsti_buffer = hsti;
		if (enter_succeeded) {
			hsti->header.status = hsti_command_status;
			hsti->state = hsti_state;
			return hsti_transport_status;
		}
		hsti->header.status = pre_hsti_command_status;
		hsti->state = 0;
		return pre_hsti_transport_status;
	}
	case MBOX_BIOS_CMD_ARMOR_SPI_TRANSACTION: {
		struct mbox_rom_armor_flash_command_buffer *transaction = buffer;

		spi_buffer = transaction;
		assert_int_equal(transaction->header.size, sizeof(*transaction));
		assert_int_equal(transaction->header.status, 0);
		assert_non_null(info_buffer);
		if (CONFIG(ASS))
			assert_ptr_equal(buffer, (void *)(uintptr_t)
					 info_buffer->req.psp_mbox_smm_buffer_address);
		if (transaction->cmd.transaction == RA_WRITE) {
			assert_int_equal(transaction->cmd.size, 1);
			assert_int_equal(*(uint8_t *)(uintptr_t)transaction->cmd.buffer_ptr,
					 expected_write_value);
			write_calls++;
		} else {
			assert_int_equal(transaction->cmd.transaction, RA_ERASE);
			assert_int_equal(transaction->cmd.size, 4 * KiB);
			erase_calls++;
		}
		if (probe_during_spi) {
			const struct region_device *writer = probe_during_spi;
			uint8_t value = 0;

			probe_during_spi = NULL;
			assert_false(psp_rom_armor_spi_transaction_available());
			assert_null(psp_rom_armor_smm_device());
			assert_int_equal(rdev_readat(writer, &value, 0, 1), -1);
			assert_int_equal(value, 0);
		}
		transaction->header.status = spi_command_status;
		return spi_transport_status;
	}
	case MBOX_BIOS_CMD_SX_INFO: {
		const struct mbox_cmd_sx_info_buffer *sx = buffer;

		assert_int_equal(sx->header.size, sizeof(*sx));
		assert_int_equal(sx->sleep_type, 3);
		sx_calls++;
		return sx_transport_status;
	}
	default:
		fail_msg("Unexpected PSP command %#x", command);
		return -1;
	}
}

msr_t rom_armor_test_rdmsr(unsigned int index)
{
	switch (index) {
	case SMM_ADDR_MSR:
		return (msr_t) { .raw = 0x400000 };
	case SMM_MASK_MSR:
		return (msr_t) { .raw = 0xffe00007 };
	default:
		fail_msg("Unexpected MSR %#x", index);
		return (msr_t) { 0 };
	}
}

void soc_fill_smm_trig_info(struct smm_trigger_info *trig)
{
	trig->address = 0x1234;
}

void soc_fill_smm_reg_info(struct smm_register_info *reg)
{
	reg->eos.address = 0x5678;
}

void __noreturn die(const char *message, ...)
{
	mock_assert(false, message, __FILE__, __LINE__);
	__builtin_unreachable();
}

bool smm_region_overlaps_handler(const struct region *region)
{
	return false;
}

uint32_t lpc_get_rom2_region(size_t *bios_size)
{
	*bios_size = 16 * MiB;
	return (uintptr_t)mapped_flash;
}

uint64_t lpc_get_rom3_region(size_t *bios_size)
{
	*bios_size = sizeof(mapped_flash);
	return (uintptr_t)mapped_flash;
}

uint32_t fch_spi_get_rom2_page(uint32_t rom2_base)
{
	assert_false(enter_succeeded);
	assert_int_equal(rom2_base, (uintptr_t)mapped_flash);
	return 0;
}

uint64_t fch_spi_get_rom3_page(uint64_t rom3_base)
{
	assert_false(enter_succeeded);
	return rom3_base - (uintptr_t)mapped_flash;
}

bool fch_spi_rom3_maps_to_bank3(void)
{
	assert_false(enter_succeeded);
	return false;
}

bool fch_spi_rom_32bit(void)
{
	assert_false(enter_succeeded);
	return true;
}

void fch_spi_configure_4dw_burst(void)
{
	assert_false(enter_succeeded);
	fch_burst_calls++;
}

void clflush_region(const uintptr_t start, const size_t size)
{
	assert_true(enter_succeeded);
	assert_int_equal(start, (uintptr_t)mapped_flash);
	assert_true(size == 1 || size == 4 * KiB);
	cache_flushes++;
}

static uint32_t public_init(bool capsule_update)
{
	struct rom_armor_params_init params = { .capsule_update = capsule_update };

	return rom_armor_exec(ROM_ARMOR_APM_CMD_INIT, &params);
}

static void check_raw_closed(void)
{
	uint8_t value = 0x5a;
	struct rom_armor_params_read read = { .buf = &value, .size = 1 };
	struct rom_armor_params_write write = { .buf = &value, .size = 1 };
	struct rom_armor_params_erase erase = { .size = 4 * KiB };
	const unsigned int old_write_calls = write_calls, old_erase_calls = erase_calls;

	assert_int_equal(public_init(false), ROM_ARMOR_RET_SHUTDOWN);
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_READ, &read), ROM_ARMOR_RET_SHUTDOWN);
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_WRITE, &write), ROM_ARMOR_RET_SHUTDOWN);
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_ERASE, &erase), ROM_ARMOR_RET_SHUTDOWN);
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_SHUTDOWN, NULL), ROM_ARMOR_RET_SHUTDOWN);
	assert_int_equal(write_calls, old_write_calls);
	assert_int_equal(erase_calls, old_erase_calls);
}

static void writer_matches_registered_owner(void **state)
{
	uint8_t value = 0x5a;
	struct rom_armor_params_write write = { .buf = &value, .size = 1 };
	const struct region_device *writer;

	assert_null(psp_rom_armor_smm_device());
	assert_int_equal(public_init(true), ROM_ARMOR_RET_SUCCESS);
	assert_int_equal(enter_buffer->capsule_update, 1);
	assert_int_equal(enter_calls, 1);
	assert_int_equal(hsti_calls, 3);
	assert_int_equal(fch_burst_calls, 1);
	/* Later ROM3 and cache-invalidation accesses must use the receipt. */
	hsti_transport_status = -PSPSTS_CMD_TIMEOUT;
	assert_null(psp_rom_armor_smm_device());
	assert_int_equal(psp_notify_smm(), 0);
	assert_int_equal(psp_notify_smm(), 0);
	assert_true(psp_smm_info_registered());
	assert_int_equal(info_calls, 1);
	assert_null(psp_rom_armor_smm_device());
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_WRITE, &write), ROM_ARMOR_RET_SUCCESS);

	assert_true(psp_rom_armor_seal());
	assert_true(psp_rom_armor_seal());
	check_raw_closed();
	writer = psp_rom_armor_smm_device();
	assert_ptr_equal(writer, &rom_armor_smm_rw);
	assert_int_equal(region_device_sz(writer), CONFIG_ROM_SIZE);
	mapped_flash[16 * MiB] = 0xa5;
	assert_int_equal(rdev_readat(writer, &value, 16 * MiB, 1), 1);
	assert_int_equal(value, 0xa5);
	value = 0x5a;
	assert_int_equal(rdev_writeat(writer, &value, 0, 1), 1);
	assert_int_equal(rdev_eraseat(writer, 0, 4 * KiB), 4 * KiB);
	assert_int_equal(write_calls, 2);
	assert_int_equal(erase_calls, 1);
	assert_int_equal(cache_flushes, 3);
	assert_int_equal(hsti_calls, 3);
	assert_int_equal(fch_burst_calls, 1);
}

static void seal_closes_uninitialized_path(void **state)
{
	assert_false(psp_rom_armor_seal());
	assert_false(psp_rom_armor_seal());
	check_raw_closed();
	assert_int_equal(psp_notify_smm(), 0);
	assert_false(psp_rom_armor_seal());
	assert_null(psp_rom_armor_smm_device());
	assert_int_equal(enter_calls, 0);
}

static void public_shutdown_closes_raw_path(void **state)
{
	assert_int_equal(public_init(false), ROM_ARMOR_RET_SUCCESS);
	assert_int_equal(psp_notify_smm(), 0);
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_SHUTDOWN, NULL), ROM_ARMOR_RET_SUCCESS);
	check_raw_closed();
	assert_true(psp_rom_armor_seal());
}

static void check_registration_failure(void)
{
	struct mbox_cmd_smm_info_buffer first;

	assert_int_equal(public_init(false), ROM_ARMOR_RET_SUCCESS);
	assert_true(psp_notify_smm() < 0);
	memcpy(&first, info_buffer, sizeof(first));
	/* A late response must not promote or cause the original request to be reused. */
	info_buffer->header.status = 0;
	first.header.status = 0;
	assert_true(psp_notify_smm() < 0);
	assert_int_equal(info_calls, 1);
	assert_memory_equal(info_buffer, &first, sizeof(first));
	assert_false(psp_smm_info_registered());
	assert_false(psp_rom_armor_seal());
	assert_null(psp_rom_armor_smm_device());
	check_raw_closed();
}

static void registration_transport_failure(void **state)
{
	info_transport_status = -PSPSTS_CMD_TIMEOUT;
	check_registration_failure();
}

static void registration_command_failure(void **state)
{
	info_command_status = 1;
	check_registration_failure();
}

static void check_ownership_failure(unsigned int expected_hsti_calls)
{
	struct mbox_rom_armor_enforce_buffer first;

	assert_int_equal(psp_notify_smm(), 0);
	assert_int_equal(public_init(true), ROM_ARMOR_RET_FAILURE);
	memcpy(&first, enter_buffer, sizeof(first));
	assert_int_equal(first.capsule_update, 1);
	assert_int_equal(public_init(false), ROM_ARMOR_RET_FAILURE);
	assert_int_equal(enter_calls, 1);
	assert_int_equal(fch_burst_calls, 1);
	assert_int_equal(hsti_calls, 2 + expected_hsti_calls);
	expect_assert_failure(boot_device_ro());
	assert_int_equal(fch_burst_calls, 1);
	assert_int_equal(hsti_calls, 2 + expected_hsti_calls);
	assert_memory_equal(enter_buffer, &first, sizeof(first));
	assert_int_equal(region_device_sz(&rom_armor_smm_rw), CONFIG_ROM_SIZE);
	assert_false(psp_rom_armor_seal());
	assert_null(psp_rom_armor_smm_device());
	check_raw_closed();
}

static void init_transport_failure(void **state)
{
	enter_transport_status = -PSPSTS_CMD_TIMEOUT;
	check_ownership_failure(0);
}

static void init_command_failure(void **state)
{
	enter_command_status = 1;
	check_ownership_failure(0);
}

static void incorrect_geometry(void **state)
{
	flash_size -= 4 * KiB;
	check_ownership_failure(0);
}

static void hsti_transport_failure(void **state)
{
	hsti_transport_status = -PSPSTS_CMD_TIMEOUT;
	check_ownership_failure(1);
}

static void hsti_command_failure(void **state)
{
	hsti_command_status = 1;
	check_ownership_failure(1);
}

static void hsti_ownership_missing(void **state)
{
	hsti_state = 0;
	check_ownership_failure(1);
}

static void check_preownership_hsti_failure(void)
{
	struct mbox_cmd_hsti_query_buffer late_response;
	uint32_t state = 0;

	expect_assert_failure(boot_device_ro());
	assert_int_equal(hsti_calls, 1);
	assert_int_equal(fch_burst_calls, 0);
	/* A late PSP write cannot turn a failed request into a receipt. */
	hsti_buffer->header.status = 0;
	hsti_buffer->state = HSTI_STATE_ROM_ARMOR_ENFORCED;
	memcpy(&late_response, hsti_buffer, sizeof(late_response));
	assert_int_equal(psp_get_hsti_state(&state), CB_ERR);
	expect_assert_failure(boot_device_ro());
	assert_int_equal(hsti_calls, 1);
	assert_int_equal(fch_burst_calls, 0);
	assert_memory_equal(hsti_buffer, &late_response, sizeof(late_response));
	assert_int_equal(enter_calls, 0);
}

static void preownership_hsti_transport_failure(void **state)
{
	pre_hsti_transport_status = -PSPSTS_CMD_TIMEOUT;
	check_preownership_hsti_failure();
}

static void preownership_hsti_command_failure(void **state)
{
	pre_hsti_command_status = 1;
	check_preownership_hsti_failure();
}

static void pending_ownership_cannot_touch_spi(void **state)
{
	probe_during_enter = true;
	assert_int_equal(public_init(false), ROM_ARMOR_RET_SUCCESS);
	assert_int_equal(hsti_calls, 3);
	assert_int_equal(fch_burst_calls, 1);
	assert_non_null(boot_device_ro());
	assert_int_equal(hsti_calls, 3);
	assert_int_equal(fch_burst_calls, 1);
}

static const struct region_device *register_and_seal_writer(void)
{
	const struct region_device *writer;

	assert_int_equal(public_init(false), ROM_ARMOR_RET_SUCCESS);
	assert_int_equal(psp_notify_smm(), 0);
	assert_true(psp_rom_armor_seal());
	writer = psp_rom_armor_smm_device();
	assert_non_null(writer);
	return writer;
}

static void spi_uncertain_completion_reserves_buffers(void **state)
{
	const struct region_device *writer = register_and_seal_writer();
	uint8_t first = 0x5a;
	uint8_t retry = 0x6b;
	uint8_t output = 0;
	struct mbox_rom_armor_flash_command direct_retry = {
		.transaction = RA_WRITE,
		.offset = 4 * KiB,
		.size = 1,
		.buffer_ptr = (uintptr_t)&retry,
	};
	uint8_t request[C2P_BUFFER_MAXSIZE];
	const uint8_t *transfer;

	probe_during_spi = writer;
	spi_transport_status = (intptr_t)*state;
	assert_int_equal(rdev_writeat(writer, &first, 0, 1), -1);
	assert_null(probe_during_spi);
	assert_int_equal(write_calls, 1);
	memcpy(request, spi_buffer, sizeof(request));
	transfer = (const void *)(uintptr_t)spi_buffer->cmd.buffer_ptr;
	assert_int_equal(*transfer, first);

	spi_transport_status = 0;
	expected_write_value = retry;
	assert_false(psp_rom_armor_spi_transaction_available());
	assert_true(psp_smm_info_registered());
	assert_false(psp_rom_armor_seal());
	assert_null(psp_rom_armor_smm_device());
	assert_int_equal(rdev_writeat(writer, &retry, 0, 1), -1);
	assert_int_equal(rdev_eraseat(writer, 0, 4 * KiB), -1);
	assert_int_equal(rdev_readat(writer, &output, 0, 1), -1);
	assert_int_equal(output, 0);
	assert_int_equal(psp_rom_armor_spi_transaction(&direct_retry), -1);
	psp_notify_sx_info(3);
	assert_int_equal(sx_calls, 0);
	assert_int_equal(write_calls, 1);
	assert_int_equal(erase_calls, 0);
	assert_int_equal(*transfer, first);
	assert_memory_equal(spi_buffer, request, sizeof(request));
	assert_int_equal(hsti_calls, 3);
	assert_int_equal(fch_burst_calls, 1);
}

static void spi_terminal_error_before_seal(void **state)
{
	uint8_t value = 0x5a;
	struct rom_armor_params_write write = { .buf = &value, .size = 1 };

	assert_int_equal(public_init(false), ROM_ARMOR_RET_SUCCESS);
	assert_int_equal(psp_notify_smm(), 0);
	spi_transport_status = -PSPSTS_SPI_COMMAND_ERROR;
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_WRITE, &write),
			 ROM_ARMOR_RET_FAILURE);
	assert_int_equal(write_calls, 1);
	assert_true(psp_rom_armor_seal());
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_WRITE, &write),
			 ROM_ARMOR_RET_SHUTDOWN);
	spi_transport_status = 0;
	assert_int_equal(rdev_writeat(psp_rom_armor_smm_device(), &value, 0, 1), 1);
	assert_int_equal(write_calls, 2);
}

static void spi_completed_command_error_allows_retry(void **state)
{
	const struct region_device *writer = register_and_seal_writer();
	uint8_t value = 0x5a;

	spi_command_status = 1;
	assert_int_equal(rdev_writeat(writer, &value, 0, 1), -1);
	assert_int_equal(write_calls, 1);
	assert_true(psp_rom_armor_spi_transaction_available());
	assert_ptr_equal(psp_rom_armor_smm_device(), writer);

	spi_command_status = 0;
	value = 0x6b;
	expected_write_value = value;
	assert_int_equal(rdev_writeat(writer, &value, 0, 1), 1);
	assert_int_equal(write_calls, 2);
	assert_int_equal(*(const uint8_t *)(uintptr_t)spi_buffer->cmd.buffer_ptr, value);
	assert_ptr_equal(psp_rom_armor_smm_device(), writer);
}

static void sx_timeout_prevents_c2p_reuse(void **state)
{
	const struct region_device *writer = register_and_seal_writer();
	const void *c2p = (const void *)(uintptr_t)info_buffer->req.psp_mbox_smm_buffer_address;
	uint8_t request[C2P_BUFFER_MAXSIZE];
	uint8_t value = 0x5a;

	sx_transport_status = -PSPSTS_CMD_TIMEOUT;
	psp_notify_sx_info(3);
	assert_int_equal(sx_calls, 1);
	memcpy(request, c2p, sizeof(request));
	assert_null(psp_rom_armor_smm_device());
	assert_int_equal(rdev_writeat(writer, &value, 0, 1), -1);
	assert_int_equal(write_calls, 0);
	psp_notify_sx_info(3);
	assert_int_equal(sx_calls, 1);
	assert_memory_equal(c2p, request, sizeof(request));
}

static void legacy_resizes_flash(void **state)
{
	flash_size -= 4 * KiB;
	info_command_status = 1;
	assert_int_equal(psp_notify_smm(), 0);
	assert_false(psp_smm_info_registered());
	assert_int_equal(psp_notify_smm(), 0);
	assert_int_equal(info_calls, 2);
	enter_transport_status = -PSPSTS_INIT_TIMEOUT;
	assert_int_equal(public_init(false), ROM_ARMOR_RET_FAILURE);
	enter_transport_status = 0;
	assert_int_equal(public_init(false), ROM_ARMOR_RET_SUCCESS);
	assert_int_equal(enter_calls, 2);
	assert_int_equal(hsti_calls, 4);
	assert_int_equal(fch_burst_calls, 2);
	assert_int_equal(region_device_sz(&rom_armor_smm_rw), flash_size);

	uint8_t value = 0x5a;
	struct rom_armor_params_write write = { .buf = &value, .size = 1 };

	spi_transport_status = -PSPSTS_CMD_TIMEOUT;
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_WRITE, &write), ROM_ARMOR_RET_FAILURE);
	assert_true(psp_rom_armor_spi_transaction_available());
	value = 0x6b;
	expected_write_value = value;
	spi_transport_status = 0;
	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_WRITE, &write), ROM_ARMOR_RET_SUCCESS);
	assert_int_equal(write_calls, 2);

	assert_int_equal(rom_armor_exec(ROM_ARMOR_APM_CMD_SHUTDOWN, NULL), ROM_ARMOR_RET_SUCCESS);
	check_raw_closed();
	assert_null(psp_rom_armor_smm_device());
}

int main(void)
{
	const struct CMUnitTest cases[] = {
		cmocka_unit_test(writer_matches_registered_owner),
		cmocka_unit_test(seal_closes_uninitialized_path),
		cmocka_unit_test(public_shutdown_closes_raw_path),
		cmocka_unit_test(registration_transport_failure),
		cmocka_unit_test(registration_command_failure),
		cmocka_unit_test(init_transport_failure),
		cmocka_unit_test(init_command_failure),
		cmocka_unit_test(incorrect_geometry),
		cmocka_unit_test(hsti_transport_failure),
		cmocka_unit_test(hsti_command_failure),
		cmocka_unit_test(hsti_ownership_missing),
		cmocka_unit_test(preownership_hsti_transport_failure),
		cmocka_unit_test(preownership_hsti_command_failure),
		cmocka_unit_test(pending_ownership_cannot_touch_spi),
		{ "spi_timeout_reserves_request_and_transfer", spi_uncertain_completion_reserves_buffers,
		  NULL, NULL, (void *)-PSPSTS_CMD_TIMEOUT },
		{ "spi_ambiguous_status_reserves_buffers", spi_uncertain_completion_reserves_buffers,
		  NULL, NULL, (void *)-PSPSTS_SEND_ERROR },
		cmocka_unit_test(spi_completed_command_error_allows_retry),
		cmocka_unit_test(spi_terminal_error_before_seal),
		cmocka_unit_test(sx_timeout_prevents_c2p_reuse),
	};
	const struct CMUnitTest legacy[] = { cmocka_unit_test(legacy_resizes_flash) };
	int result = 0;

	if (!CONFIG(ASS))
		return cb_run_group_tests(legacy, NULL, NULL);

	/* Every case starts without any SMM registration or ownership receipt. */
	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		pid_t child = fork();
		int status;

		if (child < 0)
			return 1;
		if (child == 0) {
			const struct CMUnitTest test[] = { cases[i] };
			char group[sizeof(__TEST_NAME__) + 64];

			snprintf(group, sizeof(group), "%s(%s)", __TEST_NAME__, cases[i].name);
			_exit(cmocka_run_group_tests_name(group, test, NULL, NULL) != 0);
		}
		if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status))
			result = 1;
	}
	return result;
}
