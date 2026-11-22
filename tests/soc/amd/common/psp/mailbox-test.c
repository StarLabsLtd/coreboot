/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/psp.h>
#include <tests/test.h>
#include <timer.h>
#include "psp_def.h"

#define MAILBOX_BASE 0x100000
#define READY BIT(31)
#define RECOVERY BIT(30)
#define PLATFORM_FLAGS (BIT(29) | BIT(25))

static uint32_t mailbox, written_command;
static uint32_t completion_word;
static uint64_t written_buffer;
static unsigned int command_writes;
static bool complete_command, change_after_completion;

uint64_t psp_get_base(void)
{
	return MAILBOX_BASE;
}

uint32_t psp_read32(uint64_t base, uint32_t offset)
{
	assert_int_equal(base, MAILBOX_BASE);
	assert_int_equal(offset, CONFIG_PSPV2_MBOX_CMD_OFFSET);
	uint32_t sample = mailbox;
	if (change_after_completion && command_writes)
		mailbox = 0;
	return sample;
}

void psp_write32(uint64_t base, uint32_t offset, uint32_t value)
{
	assert_int_equal(base, MAILBOX_BASE);
	if (offset == CONFIG_PSPV2_MBOX_CMD_OFFSET + 4) {
		written_buffer = (written_buffer & 0xffffffff00000000ULL) | value;
		return;
	}
	if (offset == CONFIG_PSPV2_MBOX_CMD_OFFSET + 8) {
		written_buffer = (written_buffer & UINT32_MAX) | ((uint64_t)value << 32);
		return;
	}
	assert_int_equal(offset, CONFIG_PSPV2_MBOX_CMD_OFFSET);
	written_command = value;
	command_writes++;
	/* Model completion by clearing the command and reporting delivery status. */
	mailbox = complete_command ?
		(value & (PLATFORM_FLAGS | RECOVERY)) | completion_word : value;
}

void timer_monotonic_get(struct mono_time *time)
{
	static uint64_t usecs;

	mono_time_set_usecs(time, usecs);
	usecs += USECS_PER_SEC;
}

static void reset_mailbox(uint32_t flags, uint16_t status)
{
	mailbox = READY | flags | 0x1234;
	written_command = 0;
	written_buffer = 0;
	command_writes = 0;
	completion_word = READY | status;
	complete_command = true;
	change_after_completion = false;
}

static void request_fields(void **state)
{
	struct mbox_default_buffer buffer = {0};
	uint32_t expected = MBOX_BIOS_CMD_BOOT_DONE << 16;
	const bool recovery = CONFIG(PSP_AB_RECOVERY) ||
		CONFIG(SOC_AMD_COMMON_BLOCK_SPI_BACKUP_SPI_FLASH);

	reset_mailbox(PLATFORM_FLAGS | (recovery ? RECOVERY : 0), 0);
	assert_int_equal(send_psp_command(MBOX_BIOS_CMD_BOOT_DONE, &buffer), 0);
	if (recovery)
		expected |= RECOVERY;
	assert_int_equal(written_command, expected);
	assert_int_equal(command_writes, 1);
	assert_ptr_equal((void *)(uintptr_t)written_buffer, &buffer);
}

static void delivery_error(void **state)
{
	struct mbox_default_buffer buffer = {0};

	reset_mailbox(PLATFORM_FLAGS, 0x42);
	assert_int_equal(send_psp_command(MBOX_BIOS_CMD_BOOT_DONE, &buffer),
			 -PSPSTS_SEND_ERROR);
	assert_int_equal(command_writes, 1);
}

static void recovery_mode_requires_support(void **state)
{
	struct mbox_default_buffer buffer = {0};
	const bool recovery = CONFIG(PSP_AB_RECOVERY) ||
		CONFIG(SOC_AMD_COMMON_BLOCK_SPI_BACKUP_SPI_FLASH);

	reset_mailbox(RECOVERY | PLATFORM_FLAGS, 0);
	assert_int_equal(send_psp_command(MBOX_BIOS_CMD_BOOT_DONE, &buffer),
			 recovery ? 0 : -PSPSTS_RECOVERY);
	assert_int_equal(command_writes, recovery ? 1 : 0);
}

static void not_ready_does_not_submit(void **state)
{
	struct mbox_default_buffer buffer = {0};

	reset_mailbox(0, 0);
	mailbox = 0;
	assert_int_equal(send_psp_command(MBOX_BIOS_CMD_BOOT_DONE, &buffer),
			 -PSPSTS_INIT_TIMEOUT);
	assert_int_equal(command_writes, 0);
	assert_int_equal(written_buffer, 0);
}

static void submitted_timeout_keeps_command_ownership(void **state)
{
	struct mbox_default_buffer buffer = {0};

	reset_mailbox(0, 0);
	complete_command = false;
	assert_int_equal(send_psp_command(MBOX_BIOS_CMD_BOOT_DONE, &buffer),
			 -PSPSTS_CMD_TIMEOUT);
	assert_int_equal(command_writes, 1);
	assert_ptr_equal((void *)(uintptr_t)written_buffer, &buffer);
	assert_int_equal(mailbox, MBOX_BIOS_CMD_BOOT_DONE << 16);
}

static void spi_completion(void **state)
{
	struct mbox_default_buffer buffer = {0};
	const uint32_t command = MBOX_BIOS_CMD_ARMOR_SPI_TRANSACTION;

	reset_mailbox(0, 0x1004);
	change_after_completion = CONFIG(ASS);
	assert_int_equal(send_psp_command(command, &buffer),
			 CONFIG(ASS) ? -PSPSTS_SPI_COMMAND_ERROR : -PSPSTS_SEND_ERROR);
	assert_int_equal(command_writes, 1);

	reset_mailbox(0, 0);
	completion_word = 0x1004; /* Command cleared, Ready still clear. */
	assert_int_equal(send_psp_command(command, &buffer), -PSPSTS_CMD_TIMEOUT);

	if (CONFIG(ASS)) {
		const uint32_t flags[] = { BIT(28), BIT(29), RECOVERY, BIT(25) };

		for (size_t i = 0; i < ARRAY_SIZE(flags); i++) {
			reset_mailbox(0, 0x1004);
			completion_word |= flags[i];
			assert_int_equal(send_psp_command(command, &buffer), -PSPSTS_SEND_ERROR);
			reset_mailbox(0, 0);
			completion_word |= flags[i];
			assert_int_equal(send_psp_command(command, &buffer), -PSPSTS_SEND_ERROR);
		}
	}
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(request_fields),
		cmocka_unit_test(delivery_error),
		cmocka_unit_test(spi_completion),
		cmocka_unit_test(recovery_mode_requires_support),
		cmocka_unit_test(not_ready_does_not_submit),
		cmocka_unit_test(submitted_timeout_keeps_command_ownership),
	};
	return cb_run_group_tests(tests, NULL, NULL);
}
