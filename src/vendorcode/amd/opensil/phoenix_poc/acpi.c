/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <acpi/acpi.h>
#include <Sil-api.h>
#include <SilCommon.h>
#include <xSIM-api.h>
#include <FchHwAcpi-api.h>
#include <FCH/Common/FchCommon.h>

#include <amdblocks/acpi.h>

#include "../opensil.h"

void opensil_fill_fadt(acpi_fadt_t *fadt)
{
	SIL_CONTEXT SilContext = {
		.ApobBaseAddress = CONFIG_PSP_APOB_DRAM_ADDRESS,
		.SilMemBaseAddress = (uintptr_t)cbmem_find(CBMEM_ID_AMD_OPENSIL)
	};

	FCHCLASS_INPUT_BLK *fch_data = SilFindStructure(&SilContext, SilId_FchClass, 0);

	fadt->pm1a_evt_blk = fch_data->FchBldCfg.CfgAcpiPm1EvtBlkAddr;
	fadt->pm1a_cnt_blk = fch_data->FchBldCfg.CfgAcpiPm1CntBlkAddr;
	fadt->pm_tmr_blk = fch_data->FchBldCfg.CfgAcpiPmTmrBlkAddr;
	fadt->gpe0_blk = fch_data->FchBldCfg.CfgAcpiGpe0BlkAddr;
}

unsigned long add_opensil_acpi_table(unsigned long current, acpi_rsdp_t *rsdp)
{
	return current;
}
