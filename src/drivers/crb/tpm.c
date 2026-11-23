/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * This is a driver for a CRB Interface.
 *
 * The general flow looks like this:
 *
 * TPM starts in IDLE Mode
 *
 *   IDLE  --> READY --> Command Reception
 *    ^	                        |
 *    |	                        v
      -- Cmd Complete <-- Command Execution
 */

#include <console/console.h>
#include <device/mmio.h>
#include <security/tpm/tss/tcg-2.0/tss_structures.h>
#include <string.h>
#include <timer.h>

#include "tpm.h"

struct crb_buffers {
	uint32_t command_size;
	uint8_t *command;
	uint32_t response_size;
	uint8_t *response;
};

static bool crb_buffer_valid(uint64_t address, uint32_t size)
{
	const uint64_t base = TPM_CRB_BASE_ADDRESS;
	uint64_t offset;

	if (address < base || address > UINTPTR_MAX || !size ||
	    size - 1 > UINTPTR_MAX - address)
		return false;

	/* PTP permits the TPM to alias command and response data across localities. */
	offset = address - base;
	if (offset >= CRB_LOCALITY_COUNT * CRB_LOCALITY_SIZE)
		return false;
	offset %= CRB_LOCALITY_SIZE;
	return offset >= CRB_REG_DATA_BUFF && size <= CRB_LOCALITY_SIZE - offset;
}

static bool crb_read_buffers(struct crb_buffers *buffers)
{
	uint64_t command, response;

	/*
	 * Intel PTT requires the host to program both buffers before each
	 * command. Its fixed buffer occupies the rest of the locality page.
	 */
	if (CONFIG(HAVE_INTEL_PTT)) {
		buffers->command_size = CRB_LOCALITY_SIZE - CRB_REG_DATA_BUFF;
		buffers->response_size = buffers->command_size;
		buffers->command = CRB_REG(CRB_REG_DATA_BUFF);
		buffers->response = buffers->command;
		return true;
	}

	buffers->command_size = read32(CRB_REG(CRB_REG_CMD_SIZE));
	command = read32(CRB_REG(CRB_REG_CMD_ADDR_LOW));
	command |= (uint64_t)read32(CRB_REG(CRB_REG_CMD_ADDR_HIGH)) << 32;
	buffers->response_size = read32(CRB_REG(CRB_REG_RESP_SIZE));
	response = read64(CRB_REG(CRB_REG_RESP_ADDR));
	if (!crb_buffer_valid(command, buffers->command_size) ||
	    !crb_buffer_valid(response, buffers->response_size)) {
		printk(BIOS_ERR, "TPM: CRB buffers fall outside the TPM data windows.\n");
		return false;
	}

	buffers->command = (uint8_t *)(uintptr_t)command;
	buffers->response = (uint8_t *)(uintptr_t)response;
	return true;
}

/* Wait for Reg to be expected Value  */
static tpm_result_t crb_wait_for_reg32(const void *addr, uint32_t timeoutMs, uint32_t mask,
			      uint32_t expectedValue)
{
	uint32_t regValue;
	struct stopwatch sw;

	// Set up a timer which breaks the loop after timeout
	stopwatch_init_msecs_expire(&sw, timeoutMs);

	while (1) {
		// Now check if the TPM is in IDLE mode
		regValue = read32(addr);

		if ((regValue & mask) == expectedValue)
			return TPM_SUCCESS;

		if (stopwatch_expired(&sw)) {
			printk(BIOS_ERR,
			       "CRB_WAIT: Error - Timed out with RegValue: %08x, Mask: %08x, Expected: %08x\n",
			       regValue, mask, expectedValue);
			return TPM_CB_TIMEOUT;
		}
	}
}

/* CRB PROBE
 *
 * Checks if the CRB Interface is ready
 */
static tpm_result_t crb_probe(void)
{
	uint64_t tpmStatus = read64(CRB_REG(CRB_REG_INTF_ID));
	printk(BIOS_SPEW, "Interface ID Reg. %llx\n", tpmStatus);

	if ((tpmStatus & CRB_INTF_REG_CAP_CRB) == 0) {
		printk(BIOS_DEBUG, "TPM: CRB Interface is not supported.\n");
		return TPM_CB_FAIL;
	}

	if ((tpmStatus & (0xf)) != 1) {
		printk(BIOS_DEBUG,
		       "TPM: CRB Interface is not active. System needs reboot in order to active TPM.\n");
		write32(CRB_REG(CRB_REG_INTF_ID), CRB_INTF_REG_INTF_SEL);
		return TPM_CB_FAIL;
	}

	write32(CRB_REG(CRB_REG_INTF_ID), CRB_INTF_REG_INTF_SEL);
	write32(CRB_REG(CRB_REG_INTF_ID), CRB_INTF_REG_INTF_LOCK);

	return TPM_SUCCESS;
}

static tpm_result_t crb_claim_locality(void)
{
	struct stopwatch sw;
	bool requested = false;
	tpm_result_t rc = crb_wait_for_reg32(CRB_REG(CRB_REG_LOC_STATE), 1500,
					   LOC_STATE_REG_VALID_STS, LOC_STATE_REG_VALID_STS);
	if (rc)
		return rc;

	/* TIS uses locality 0. Wait for any other owner to release the TPM. */
	stopwatch_init_msecs_expire(&sw, 750);
	do {
		uint32_t state = read32(CRB_REG(CRB_REG_LOC_STATE));

		if (!(state & LOC_STATE_REG_VALID_STS))
			continue;

		if (state & LOC_STATE_LOC_ASSIGN) {
			if (!(state & LOC_STATE_ACTIVE_LOC_MASK) &&
			    (read32(CRB_REG(CRB_REG_LOC_STS)) & LOC_STS_GRANTED))
				return TPM_SUCCESS;
		} else if (!requested) {
			write8(CRB_REG(CRB_REG_LOC_CTRL), LOC_CTRL_REQ_ACCESS);
			requested = true;
		}
	} while (!stopwatch_expired(&sw));

	printk(BIOS_ERR, "TPM: Could not claim CRB locality 0.\n");
	return TPM_CB_TIMEOUT;
}

/* Switch Device into a Ready State */
static tpm_result_t crb_switch_to_ready(void)
{
	/* Transition into ready state */
	write8(CRB_REG(CRB_REG_REQUEST), CRB_REG_REQUEST_CMD_RDY);
	tpm_result_t rc = crb_wait_for_reg32(CRB_REG(CRB_REG_REQUEST), 200,
					CRB_REG_REQUEST_CMD_RDY, 0x0);
	if (rc) {
		printk(BIOS_ERR,
		       "TPM Error (%#x): TPM did not transition into ready state in time.\n", rc);
		return rc;
	}

	/* Check TPM_CRB_CTRL_STS[0] to be "0" - no unrecoverable error */
	rc = crb_wait_for_reg32(CRB_REG(CRB_REG_STATUS), 500, CRB_REG_STATUS_ERROR,
				0x0);
	if (rc) {
		printk(BIOS_ERR, "TPM Error (%#x): Could not recover.\n", rc);
		return rc;
	}

	return TPM_SUCCESS;
}

tpm_result_t crb_tpm_init(void)
{
	tpm_result_t rc = crb_probe();
	if (rc) {
		printk(BIOS_ERR, "TPM: Probe failed.\n");
		return rc;
	}

	return crb_claim_locality();
}

static void set_ptt_cmd_resp_buffers(const struct crb_buffers *buffers)
{
	uint64_t command = (uintptr_t)buffers->command;

	write32(CRB_REG(CRB_REG_CMD_ADDR_HIGH), command >> 32);
	write32(CRB_REG(CRB_REG_CMD_ADDR_LOW), command);
	write32(CRB_REG(CRB_REG_CMD_SIZE), buffers->command_size);
	write64(CRB_REG(CRB_REG_RESP_ADDR), (uintptr_t)buffers->response);
	write32(CRB_REG(CRB_REG_RESP_SIZE), buffers->response_size);
}

/*
 * crb_tpm_process_command
 */
size_t crb_tpm_process_command(const void *tpm2_command, size_t command_size,
			       void *tpm2_response, size_t max_response)
{
	struct crb_buffers buffers;
	struct tpm_header header;
	const uint8_t *command = tpm2_command;
	uint8_t *response = tpm2_response;
	tpm_result_t rc;

	if (crb_claim_locality() || !crb_read_buffers(&buffers))
		return 0;

	if (command_size > buffers.command_size) {
		printk(BIOS_ERR, "TPM: Command size is too big.\n");
		return 0;
	}

	if (buffers.response_size < sizeof(header) || max_response < sizeof(header)) {
		printk(BIOS_ERR, "TPM: Response buffer cannot hold a TPM header.\n");
		return 0;
	}

	rc = crb_wait_for_reg32(CRB_REG(CRB_REG_START), 250, CRB_REG_START_START, 0);
	if (rc) {
		printk(BIOS_ERR, "TPM Error (%#x): Cmd Bit not cleared.\n", rc);
		return 0;
	}

	rc = crb_switch_to_ready();
	if (rc) {
		printk(BIOS_DEBUG, "TPM Error (%#x): Can not transition into ready state.\n", rc);
		return 0;
	}

	for (size_t i = 0; i < command_size; i++)
		write8(buffers.command + i, command[i]);

	/*
	 * Initialize CRB addresses and sizes for PTT. It seems to be possible
	 * only after CRB is switched to ready and before writing start bit.
	 * This is also what EDK2 TPM CRB drivers do.
	 */
	if (CONFIG(HAVE_INTEL_PTT))
		set_ptt_cmd_resp_buffers(&buffers);

	write8(CRB_REG(CRB_REG_START), CRB_REG_START_START);

	// Poll for Response
	rc = crb_wait_for_reg32(CRB_REG(CRB_REG_START), 3500, CRB_REG_START_START, 0);
	if (rc) {
		printk(BIOS_DEBUG, "TPM Error (%#x): Command Timed out.\n", rc);
		return 0;
	}

	// Check for errors
	rc = crb_wait_for_reg32(CRB_REG(CRB_REG_STATUS), 200, CRB_REG_STATUS_ERROR, 0);
	if (rc) {
		printk(BIOS_DEBUG, "TPM Error (%#x): Command errored.\n", rc);
		return 0;
	}

	/* Read the response from the reported base, in order and without rereads. */
	for (size_t i = 0; i < sizeof(header); i++)
		((uint8_t *)&header)[i] = read8(buffers.response + i);

	uint32_t length = be32_to_cpu(header.tpm_size);
	if (length < sizeof(header) || length > buffers.response_size ||
	    length > max_response) {
		printk(BIOS_ERR, "TPM: Invalid CRB response length %u.\n", length);
		length = 0;
	} else {
		memcpy(response, &header, sizeof(header));
		for (size_t i = sizeof(header); i < length; i++)
			response[i] = read8(buffers.response + i);
	}

	rc = crb_switch_to_ready();
	if (rc) {
		printk(BIOS_DEBUG, "TPM Error (%#x): Can not transition into ready state again.\n", rc);
		return 0;
	}

	return length;
}

/*
 * tp2_get_info
 *
 * Returns information about the TPM
 *
 */
void crb_tpm_get_info(struct crb_tpm_info *crb_tpm_info)
{
	uint64_t interfaceReg = read64(CRB_REG(CRB_REG_INTF_ID));

	crb_tpm_info->vendor_id = (interfaceReg >> 48) & 0xFFFF;
	crb_tpm_info->device_id = (interfaceReg >> 32) & 0xFFFF;
	crb_tpm_info->revision = (interfaceReg >> 24) & 0xFF;
}

/*
 * crb_tpm_is_active
 *
 * Checks that CRB interface is available and active.
 *
 * The body was derived from crb_probe() which unlike this function can also
 * write to registers.
 */
bool crb_tpm_is_active(void)
{
	uint64_t tpmStatus = read64(CRB_REG(CRB_REG_INTF_ID));
	printk(BIOS_SPEW, "Interface ID Reg. %llx\n", tpmStatus);

	if ((tpmStatus & CRB_INTF_REG_CAP_CRB) == 0) {
		printk(BIOS_DEBUG, "TPM: CRB Interface is not supported.\n");
		return false;
	}

	if ((tpmStatus & (0xf)) != 1) {
		printk(BIOS_DEBUG, "TPM: CRB Interface is not active.\n");
		return false;
	}

	return true;
}

/*
 * crb_tpm_base_address
 *
 * Returns the CRB TPM base address.
 */
uintptr_t crb_tpm_base_address(void)
{
	return CONFIG_CRB_TPM_BASE_ADDRESS;
}
