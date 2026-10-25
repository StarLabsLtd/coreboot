/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <commonlib/region.h>
#include <cpu/x86/smm.h>
#include <smmstore.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
#if CONFIG(SMMSTORE_SRK_PIN)
	static void *com_buffer;
#endif

	/* Do not reopen capsule access after the S3 handler is reloaded. */
	if (CONFIG(SMMSTORE_SRK_PIN) && smm_is_s3_resume() &&
	    (command & SMMSTORE_CMD_USE_FULL_FLASH))
		return SMMSTORE_RET_UNSUPPORTED;

	int handled = smmstore_preprocess_cmd(&command, param);

	if (handled)
		return handled > 0 ? SMMSTORE_RET_SUCCESS : SMMSTORE_RET_DENIED;

	if (!param)
		return SMMSTORE_RET_FAILURE;

	if (!initialized) {
		uintptr_t base;
		size_t size;
		smm_get_smmstore_com_buffer(&base, &size);

		if (smmstore_init((void *)base, size))
			return SMMSTORE_RET_FAILURE;
#if CONFIG(SMMSTORE_SRK_PIN)
		com_buffer = (void *)base;
#endif
		initialized = true;
	}

	switch (command) {
#if CONFIG(SMMSTORE_SRK_PIN)
	case SMMSTORE_CMD_SRK_READ:
	case SMMSTORE_CMD_SRK_CREATE:
	case SMMSTORE_CMD_SECURITY_CLOSE:
		return smmstore_srk_exec(command, com_buffer);
#endif
	case SMMSTORE_CMD_RAW_READ: {
		printk(BIOS_DEBUG, "Raw read from SMM store, param = %p\n", param);
		struct smmstore_params_raw_read params;

		if (range_check(param, sizeof(params)) != 0)
			break;
		memcpy(&params, param, sizeof(params));

		if (smmstore_rawread_region(params.block_id, params.bufoffset,
					    params.bufsize) == 0)
			ret = SMMSTORE_RET_SUCCESS;
		break;
	}
	case SMMSTORE_CMD_RAW_WRITE: {
		printk(BIOS_DEBUG, "Raw write to SMM store, param = %p\n", param);
		struct smmstore_params_raw_write params;

		if (range_check(param, sizeof(params)) != 0)
			break;
		memcpy(&params, param, sizeof(params));

		if (smmstore_rawwrite_region(params.block_id, params.bufoffset,
					     params.bufsize) == 0)
			ret = SMMSTORE_RET_SUCCESS;
		break;
	}
	case SMMSTORE_CMD_RAW_CLEAR: {
		printk(BIOS_DEBUG, "Raw clear SMM store, param = %p\n", param);
		struct smmstore_params_raw_clear params;

		if (range_check(param, sizeof(params)) != 0)
			break;
		memcpy(&params, param, sizeof(params));

		if (smmstore_rawclear_region(params.block_id) == 0)
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
