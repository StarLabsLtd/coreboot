/* SPDX-License-Identifier: GPL-2.0-only */

#include <amdblocks/cppc.h>
#include <cbmem.h>
#include <xPRF-api.h>

enum cb_err get_ccx_cppc_min_frequency(uint32_t *freq)
{
	SIL_CONTEXT context = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL),
	};

	if (!context.SilMemBaseAddress)
		return CB_ERR;
	return xPrfGetCppcMinFrequency(&context, freq) == SilPass ? CB_SUCCESS : CB_ERR;
}

enum cb_err get_ccx_cppc_nom_frequency(uint32_t *freq)
{
	SIL_CONTEXT context = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL),
	};

	if (!context.SilMemBaseAddress)
		return CB_ERR;
	return xPrfGetCppcNomFrequency(&context, freq) == SilPass ? CB_SUCCESS : CB_ERR;
}
