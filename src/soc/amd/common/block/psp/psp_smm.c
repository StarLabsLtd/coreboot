/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/mmio.h>
#include <cpu/x86/msr.h>
#include <cpu/amd/msr.h>
#include <region_file.h>
#include <console/console.h>
#include <amdblocks/psp.h>
#include <amdblocks/smi.h>
#include <boot_device.h>
#include <soc/iomap.h>
#include <spi_flash.h>
#include <string.h>

#include "psp_def.h"

/*
 * When sending PSP mailbox commands to the PSP from the SMI handler after the boot done
 * command was sent, the corresponding data buffer needs to be placed in this core to PSP (C2P)
 * buffer.
 */
struct {
	uint8_t buffer[C2P_BUFFER_MAXSIZE];
} __aligned(32) c2p_buffer;

/*
 * When the PSP sends mailbox commands to the host, it will update the PSP to core (P2C) buffer
 * and then send an SMI to the host to process the request.
 */
struct {
	uint8_t buffer[P2C_BUFFER_MAXSIZE];
} __aligned(32) p2c_buffer;

/*
 * When sending PSP mailbox commands to the PSP from the SMI handler, the SMM flag needs to be
 * set for the PSP to accept it. Otherwise it should be cleared.
 */
static uint32_t smm_flag;
static bool ass_smm_info_registered;

bool psp_smm_info_registered(void)
{
	return ass_smm_info_registered;
}

void psp_set_smm_flag(void)
{
	smm_flag = 1;
}

void psp_clear_smm_flag(void)
{
	smm_flag = 0;
}

/*
 * The MBOX_BIOS_CMD_SMM_INFO PSP mailbox command doesn't necessarily need be sent from SMM,
 * but doing so allows the linker to sort out the addresses of c2p_buffer, p2c_buffer and
 * smm_flag without us needing to pass this info between ramstage and smm. In the PSP gen2 case
 * this will also make sure that the PSP MMIO base will be cached in SMM before the OS takes
 * over so no SMN accesses will be needed during OS runtime.
 */
int psp_notify_smm(void)
{
	static struct mbox_cmd_smm_info_buffer ass_buffer;
	static bool attempted;
	struct mbox_cmd_smm_info_buffer local_buffer;
	struct mbox_cmd_smm_info_buffer *buffer = &local_buffer;
	msr_t msr;
	int cmd_status;

	if (CONFIG(ASS)) {
		/* A timed-out command may still reference its buffer. */
		if (attempted)
			return ass_smm_info_registered ? 0 : -PSPSTS_SEND_ERROR;
		attempted = true;
		buffer = &ass_buffer;
	}

	*buffer = (struct mbox_cmd_smm_info_buffer) {
		.header.size = sizeof(*buffer),
		.req = {
			.psp_smm_data_region = (uintptr_t)p2c_buffer.buffer,
			.psp_smm_data_length = sizeof(p2c_buffer),
			.psp_mbox_smm_buffer_address = (uintptr_t)c2p_buffer.buffer,
			.psp_mbox_smm_flag_address = (uintptr_t)&smm_flag,
		},
	};

	msr = rdmsr(SMM_ADDR_MSR);
	buffer->req.smm_base = msr.raw;
	msr = rdmsr(SMM_MASK_MSR);
	msr.lo &= 0xffff0000; /* mask SMM_TSEG_VALID and reserved bits */
	buffer->req.smm_mask = msr.raw;

	soc_fill_smm_trig_info(&buffer->req.smm_trig_info);
#if (CONFIG(SOC_AMD_COMMON_BLOCK_PSP_GEN2))
	soc_fill_smm_reg_info(&buffer->req.smm_reg_info);
#endif

	if (CONFIG(SOC_AMD_COMMON_BLOCK_PSP_SMI)) {
		configure_psp_smi();
		enable_psp_smi();

		/* Probe for SPI flash now as it's likely not busy */
		assert(boot_device_spi_flash());
	}

	printk(BIOS_DEBUG, "PSP: Notify SMM info... ");
	cmd_status = send_psp_command(MBOX_BIOS_CMD_SMM_INFO, buffer);
	psp_print_cmd_status(cmd_status, &buffer->header);

	if (CONFIG(ASS)) {
		if (cmd_status)
			return cmd_status;
		if (buffer->header.status)
			return -PSPSTS_SEND_ERROR;
		ass_smm_info_registered = true;
	}

	return cmd_status;
}

static bool ass_rom_armor_entry_attempted;
static bool ass_rom_armor_enforced;

bool psp_smm_rom_armor_enforced(void)
{
	if (ass_rom_armor_enforced)
		return true;
	if (ass_rom_armor_entry_attempted)
		die("PSP: ROM Armor ownership unconfirmed after Enter SMM Mode\n");
	return false;
}

int psp_rom_armor_enter_smm_mode(const bool allow_capsule_update, size_t *flash_size)
{
	static struct mbox_rom_armor_enforce_buffer ass_buffer;
	struct mbox_rom_armor_enforce_buffer local_buffer;
	struct mbox_rom_armor_enforce_buffer *buffer = &local_buffer;
	const struct region_device *flash;
	uint32_t hsti_state;
	int cmd_status;

	*flash_size = 0;

	/* Keep the one-shot request intact even if the PSP times out. */
	if (CONFIG(ASS) && ass_rom_armor_entry_attempted)
		return -1;

	/* Set up the flash mapping while the LPC registers are still accessible. */
	flash = boot_device_ro();
	if (CONFIG(ASS)) {
		if (!flash || region_device_sz(flash) != CONFIG_ROM_SIZE)
			return -1;
		ass_rom_armor_entry_attempted = true;
		buffer = &ass_buffer;
	}

	*buffer = (struct mbox_rom_armor_enforce_buffer) {
		.header.size = sizeof(*buffer),
		.capsule_update = allow_capsule_update,
	};

	printk(BIOS_SPEW, "PSP: Entering ROM Armor SMM-only mode...\n");
	cmd_status = send_psp_command(MBOX_BIOS_CMD_ARMOR_ENTER_SMM_MODE, buffer);
	psp_print_cmd_status(cmd_status, &buffer->header);

	if (cmd_status || buffer->header.status)
		return -1;

	if (CONFIG(ASS)) {
		if (buffer->flash_size != CONFIG_ROM_SIZE) {
			printk(BIOS_ERR, "PSP: ROM Armor flash size 0x%x doesn't match CONFIG_ROM_SIZE!\n",
			       buffer->flash_size);
			return -1;
		}
		if (psp_get_hsti_state(&hsti_state) != CB_SUCCESS ||
		    !(hsti_state & HSTI_STATE_ROM_ARMOR_ENFORCED)) {
			printk(BIOS_ERR, "PSP: ASS ROM Armor enforcement not confirmed\n");
			return -1;
		}
		ass_rom_armor_enforced = true;
	}

	*flash_size = buffer->flash_size;
	return 0;
}

static bool ass_c2p_unavailable;

bool psp_rom_armor_spi_transaction_available(void)
{
	return !CONFIG(ASS) || !ass_c2p_unavailable;
}

static int send_c2p_command(uint32_t command, void *buffer)
{
	if (CONFIG(ASS))
		ass_c2p_unavailable = true;

	int status = send_psp_command(command, buffer);

	/* Keep both buffers reserved unless the PSP finished this command. */
	if (CONFIG(ASS) && (!status || (command == MBOX_BIOS_CMD_ARMOR_SPI_TRANSACTION &&
				       status == -PSPSTS_SPI_COMMAND_ERROR)))
		ass_c2p_unavailable = false;

	return status;
}

int psp_rom_armor_spi_transaction(const struct mbox_rom_armor_flash_command *cmd_buf)
{
	int cmd_status;
	struct mbox_rom_armor_flash_command_buffer *buffer;

	if (!psp_rom_armor_spi_transaction_available())
		return -1;

	/* PSP verifies that this buffer is at the address specified in psp_notify_smm() */
	buffer = (struct mbox_rom_armor_flash_command_buffer *)c2p_buffer.buffer;
	assert(buffer);
	assert(cmd_buf);

	buffer->header.size = sizeof(*buffer);
	buffer->header.status = 0; /* Clear status before sending command */
	memcpy(&buffer->cmd, cmd_buf, sizeof(*cmd_buf));

	/* Sanity checks */
	assert(buffer->cmd.transaction);
	assert(buffer->cmd.buffer_ptr);
	assert(buffer->cmd.size);

	printk(BIOS_SPEW, "PSP: Sending transaction type=%u offset=0x%x size=0x%x buffer_ptr=0x%llx read_back=0x%x\n",
	       buffer->cmd.transaction, buffer->cmd.offset, buffer->cmd.size,
	       buffer->cmd.buffer_ptr, buffer->cmd.read_back);

	asm volatile ("sfence");

	/* Send command to PSP */
	cmd_status = send_c2p_command(MBOX_BIOS_CMD_ARMOR_SPI_TRANSACTION, buffer);
	if (cmd_status) {
		psp_print_cmd_status(cmd_status, NULL);
		return cmd_status;
	}

	if (buffer->header.status) {
		psp_print_cmd_status(-PSPSTS_SEND_ERROR, &buffer->header);
		return -PSPSTS_SEND_ERROR;
	}

	return 0;
}

/* Notify PSP the system is going to a sleep state. */
void psp_notify_sx_info(uint8_t sleep_type)
{
	int cmd_status;
	struct mbox_cmd_sx_info_buffer *buffer;

	if (!psp_rom_armor_spi_transaction_available()) {
		printk(BIOS_ERR, "PSP: C2P buffer unavailable for SxInfo\n");
		return;
	}

	/* PSP verifies that this buffer is at the address specified in psp_notify_smm() */
	buffer = (struct mbox_cmd_sx_info_buffer *)c2p_buffer.buffer;
	memset(buffer, 0, sizeof(*buffer));
	buffer->header.size = sizeof(*buffer);

	if (sleep_type > MBOX_BIOS_CMD_SX_INFO_SLEEP_TYPE_MAX) {
		printk(BIOS_ERR, "PSP: BUG: invalid sleep type 0x%x requested\n", sleep_type);
		return;
	}

	printk(BIOS_DEBUG, "PSP: Prepare to enter sleep state %d... ", sleep_type);

	buffer->sleep_type = sleep_type;

	cmd_status = send_c2p_command(MBOX_BIOS_CMD_SX_INFO, buffer);

	/* buffer's status shouldn't change but report it if it does */
	psp_print_cmd_status(cmd_status, &buffer->header);
}
