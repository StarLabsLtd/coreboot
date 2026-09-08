/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <smmstore.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool variable_busy;

bool smmstore_variable_busy(void)
{
	return variable_busy || !smm_get_smmstore_generation();
}

void smmstore_variable_changed(void)
{
	(*smm_get_smmstore_generation())++;
}

/*
 * Check that the given range is legal.
 *
 * Legal means:
 *  - not pointing into SMRAM
 *
 * returns 0 on success, -1 on failure
 */
static int range_check(void *start, size_t size)
{
	if (smm_points_to_smram(start, size))
		return -1;

	return 0;
}

uint32_t smmstore_exec(uint8_t command, void *param)
{
	uint32_t ret = SMMSTORE_RET_FAILURE;
	static bool initialized = false;
	static void *com_buffer;

	/* Payload MM owns the variable store and only the update path uses SMMSTORE. */
	if (CONFIG(PAYLOAD_MM_INTERFACE) && !(command & SMMSTORE_CMD_USE_FULL_FLASH))
		return SMMSTORE_RET_UNSUPPORTED;

	if (smmstore_preprocess_cmd(&command, param))
		return SMMSTORE_RET_SUCCESS;

	if (CONFIG(PAYLOAD_MM_INTERFACE) && !smmstore_full_flash_enabled())
		return SMMSTORE_RET_UNSUPPORTED;

	if (!param)
		return SMMSTORE_RET_FAILURE;

	if (!initialized) {
		uintptr_t base;
		size_t size;
		smm_get_smmstore_com_buffer(&base, &size);

		if (smmstore_init((void *)base, size))
			return SMMSTORE_RET_FAILURE;
		if (size < sizeof(uint64_t))
			return SMMSTORE_RET_FAILURE;
		com_buffer = (void *)base;
		initialized = true;
	}

	switch (command) {
	case SMMSTORE_CMD_VARIABLE_BEGIN:
		if (!smm_get_smmstore_generation())
			return SMMSTORE_RET_FAILURE;
		if (variable_busy)
			return SMMSTORE_RET_BUSY;
		variable_busy = true;
		memcpy(com_buffer, smm_get_smmstore_generation(), sizeof(uint64_t));
		ret = SMMSTORE_RET_SUCCESS;
		break;
	case SMMSTORE_CMD_VARIABLE_END:
		if (!variable_busy)
			break;
		variable_busy = false;
		ret = SMMSTORE_RET_SUCCESS;
		break;
	case SMMSTORE_CMD_RAW_READ: {
		printk(BIOS_DEBUG, "Raw read from SMM store, param = %p\n", param);
		struct smmstore_params_raw_read *params = param;

		if (range_check(params, sizeof(*params)) != 0)
			break;

		if (smmstore_rawread_region(params->block_id, params->bufoffset,
					    params->bufsize) == 0)
			ret = SMMSTORE_RET_SUCCESS;
		break;
	}
	case SMMSTORE_CMD_RAW_WRITE: {
		printk(BIOS_DEBUG, "Raw write to SMM store, param = %p\n", param);
		struct smmstore_params_raw_write *params = param;

		if (range_check(params, sizeof(*params)) != 0)
			break;

		if (smmstore_rawwrite_region(params->block_id, params->bufoffset,
					     params->bufsize) == 0)
			ret = SMMSTORE_RET_SUCCESS;
		break;
	}
	case SMMSTORE_CMD_RAW_CLEAR: {
		printk(BIOS_DEBUG, "Raw clear SMM store, param = %p\n", param);
		struct smmstore_params_raw_clear *params = param;

		if (range_check(params, sizeof(*params)) != 0)
			break;

		if (smmstore_rawclear_region(params->block_id) == 0)
			ret = SMMSTORE_RET_SUCCESS;
		break;
	}
	default:
		printk(BIOS_DEBUG, "Unknown SMM store command: 0x%02x\n", command);
		ret = SMMSTORE_RET_UNSUPPORTED;
		break;
	}

	return ret;
}
