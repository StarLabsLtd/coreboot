/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <cpu/intel/em64t100_save_state.h>
#include <cpu/intel/em64t101_save_state.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define REV100 0x30100U
#define REV101 0x30101U

static void reset_runtime(void)
{
	memset((void *)&smm_runtime, 0, sizeof(smm_runtime));
	smm_runtime.num_cpus = 1;
}

static void em64t100_layout(void)
{
	_Alignas(em64t100_smm_state_save_area_t)
		uint8_t image[STM_PSD_SIZE + sizeof(em64t100_smm_state_save_area_t)];
	em64t100_smm_state_save_area_t *state =
		(void *)(image + STM_PSD_SIZE);

	memset(image, 0xa5, sizeof(image));
	state->smm_revision = REV100;
	reset_runtime();
	smm_runtime.save_state_size = sizeof(image);
	smm_runtime.save_state_top[0] = (uintptr_t)(image + sizeof(image));
	assert(smm_get_save_state(0) == state);
	assert(smm_revision() == REV100);
}

static void em64t101_layout(void)
{
	_Alignas(em64t101_smm_state_save_area_t)
		uint8_t image[STM_PSD_SIZE + sizeof(em64t101_smm_state_save_area_t)];
	em64t101_smm_state_save_area_t *state =
		(void *)(image + STM_PSD_SIZE);

	memset(image, 0x5a, sizeof(image));
	state->smm_revision = REV101;
	reset_runtime();
	smm_runtime.save_state_size = sizeof(image);
	smm_runtime.save_state_top[0] = (uintptr_t)(image + sizeof(image));
	assert(smm_get_save_state(0) == state);
	assert(smm_revision() == REV101);
}

#if CONFIG(STM)
static void invalid_geometry(void)
{
	reset_runtime();
	smm_runtime.save_state_size = STM_PSD_SIZE;
	smm_runtime.save_state_top[0] = UINTPTR_MAX;
	assert(smm_get_save_state(0) == NULL);
	assert(smm_revision() == SMM_REV_INVALID);

	smm_runtime.save_state_size = STM_PSD_SIZE + 16U;
	smm_runtime.save_state_top[0] = 16U;
	assert(smm_get_save_state(0) == NULL);
	assert(smm_revision() == SMM_REV_INVALID);
	assert(smm_get_save_state(-1) == NULL);
	assert(smm_get_save_state(1) == NULL);
}
#endif

int main(void)
{
	em64t100_layout();
	em64t101_layout();
#if CONFIG(STM)
	invalid_geometry();
#endif
	return 0;
}
