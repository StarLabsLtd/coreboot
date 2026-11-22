/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/psp.h>
#include <console/console.h>
#include <device/mmio.h>
#include <fmap.h>
#include <thread.h>
#include <timer.h>
#include <types.h>
#include "psp_def.h"

#define PSP_MAILBOX_COMMAND_OFFSET	CONFIG_PSPV2_MBOX_CMD_OFFSET		/* 4 bytes */
#define PSP_MAILBOX_BUFFER_OFFSET	(CONFIG_PSPV2_MBOX_CMD_OFFSET + 4)	/* 8 bytes */

union pspv2_mbox_command {
	uint32_t val;
	struct pspv2_mbox_cmd_fields {
		uint16_t mbox_status;
		uint8_t mbox_command;
		uint32_t reserved:6;
		uint32_t recovery:1;
		uint32_t ready:1;
	} __packed fields;
};

/* AsyncCmdInProgress, ResetRequired, Recovery or unknown flags preclude reuse. */
static bool spi_mailbox_idle(union pspv2_mbox_command sample)
{
	return (sample.val & 0xffff0000) == BIT(31);
}

static void wr_mbox_cmd(uint64_t base, uint8_t cmd)
{
	union pspv2_mbox_command tmp, reg = { .val = 0 };
	/*
	 * When recovery boot is supported (A/B recovery or second SPI flash) then
	 * the recovery bit must be preserved for FSP. Clear the other fields to
	 * make sure PSP starts processing the request.
	 */
	if (CONFIG(PSP_AB_RECOVERY) ||
	    CONFIG(SOC_AMD_COMMON_BLOCK_SPI_BACKUP_SPI_FLASH)) {
		tmp.val = psp_read32(base, PSP_MAILBOX_COMMAND_OFFSET);
		reg.fields.recovery = tmp.fields.recovery;
	}

	/* Write entire 32-bit area to begin command execution */
	reg.fields.mbox_command = cmd;
	psp_write32(base, PSP_MAILBOX_COMMAND_OFFSET, reg.val);
}

static uint8_t rd_mbox_recovery(uint64_t base)
{
	union pspv2_mbox_command tmp;

	tmp.val = psp_read32(base, PSP_MAILBOX_COMMAND_OFFSET);
	return !!tmp.fields.recovery;
}

static void wr_mbox_buffer_ptr(uint64_t base, void *buffer)
{
	const uint64_t address = (uintptr_t)buffer;

	/* The pointer occupies two dword registers, with only dword alignment. */
	psp_write32(base, PSP_MAILBOX_BUFFER_OFFSET, (uint32_t)address);
	psp_write32(base, PSP_MAILBOX_BUFFER_OFFSET + 4, address >> 32);
}

static int wait_command(uint64_t base, bool wait_for_ready, union pspv2_mbox_command *sample)
{
	struct stopwatch sw;

	stopwatch_init_msecs_expire(&sw, PSP_CMD_TIMEOUT);

	while (1) {
		sample->val = psp_read32(base, PSP_MAILBOX_COMMAND_OFFSET);
		if (!sample->fields.mbox_command && (!wait_for_ready || sample->fields.ready))
			return 0;

		if (stopwatch_expired(&sw))
			return -PSPSTS_CMD_TIMEOUT;

		thread_yield();
	}
}

int send_psp_command(uint32_t command, void *buffer)
{
	const uint64_t base = psp_get_base();
	const bool armor_spi = CONFIG(ASS) && command == MBOX_BIOS_CMD_ARMOR_SPI_TRANSACTION;
	union pspv2_mbox_command sample;
	int ret = 0;

	if (!base)
		return -PSPSTS_NOBASE;

	/*
	 * When recovery boot is supported (A/B recovery or second SPI flash) and
	 * when booting from recovery partition the PSP is still functional even when
	 * reporting recovery mode.
	 */
	if (!(CONFIG(PSP_AB_RECOVERY) ||
	      CONFIG(SOC_AMD_COMMON_BLOCK_SPI_BACKUP_SPI_FLASH)) &&
	      rd_mbox_recovery(base))
		return -PSPSTS_RECOVERY;

	if (wait_command(base, true, &sample))
		return -PSPSTS_INIT_TIMEOUT;
	if (armor_spi && !spi_mailbox_idle(sample))
		return -PSPSTS_SEND_ERROR;

	if (ENV_SMM)
		psp_set_smm_flag();

	/* set address of command-response buffer and write command register */
	wr_mbox_buffer_ptr(base, buffer);
	wr_mbox_cmd(base, command);

	/* PSP clears command register when complete.  All commands except
	 * SxInfo set the Ready bit. */
	if (wait_command(base, command != MBOX_BIOS_CMD_SX_INFO, &sample)) {
		ret = -PSPSTS_CMD_TIMEOUT;
		goto out;
	}

	/* Classify SPI ownership and status from the same completion sample. */
	if (!armor_spi)
		sample.val = psp_read32(base, PSP_MAILBOX_COMMAND_OFFSET);
	if (armor_spi && !spi_mailbox_idle(sample)) {
		printk(BIOS_ERR, "PSP: SPI transaction completion uncertain, mailbox=%#x\n",
		       sample.val);
		ret = -PSPSTS_SEND_ERROR;
	} else if (sample.fields.mbox_status) {
		printk(BIOS_ERR, "PSP: command %#x failed, mailbox status=%#x\n",
		       command, sample.fields.mbox_status);
		ret = armor_spi ? -PSPSTS_SPI_COMMAND_ERROR : -PSPSTS_SEND_ERROR;
	}

out:
	if (ENV_SMM)
		psp_clear_smm_flag();
	return ret;
}

enum cb_err psp_get_psp_capabilities(uint32_t *capabilities)
{
	int cmd_status;
	struct mbox_cmd_capability_query_buffer buffer = {
		.header = {
			.size = sizeof(buffer)
		},
	};

	printk(BIOS_DEBUG, "PSP: Querying PSP capabilities...");

	cmd_status = send_psp_command(MBOX_BIOS_CMD_PSP_CAPS_QUERY, &buffer);

	/* buffer's status shouldn't change but report it if it does */
	psp_print_cmd_status(cmd_status, &buffer.header);

	if (cmd_status)
		return CB_ERR;

	*capabilities = read32(&buffer.capabilities);
	return CB_SUCCESS;
}

enum cb_err soc_read_c2p38(uint32_t *msg_38_value)
{
	const uint64_t base = psp_get_base();

	if (!base) {
		printk(BIOS_WARNING, "PSP: PSP_ADDR_MSR uninitialized\n");
		return CB_ERR;
	}
	*msg_38_value = psp_read32(base, CORE_2_PSP_MSG_38_OFFSET);
	return CB_SUCCESS;
}
