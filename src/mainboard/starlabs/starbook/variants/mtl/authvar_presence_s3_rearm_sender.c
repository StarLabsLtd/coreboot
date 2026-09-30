/* SPDX-License-Identifier: GPL-2.0-only */

#include "authvar_presence_s3_rearm.h"
#include "dma_smm_receipt_provision.h"
#include "loader_instance_authority.h"

#include <acpi/acpi.h>
#include <arch/io.h>
#include <bootstate.h>
#include <console/console.h>
#include <cpu/x86/smm.h>

#if !ENV_RAMSTAGE && !ENV_TEST
#error "StarBook MTL authenticated-variable S3 rearm sender is ramstage-only"
#endif

#if ENV_TEST
uint64_t starbook_mtl_authvar_presence_s3_rearm_trigger_test(uint64_t request);
#endif

enum cb_err starbook_mtl_authvar_presence_s3_rearm_send(void)
{
	uint64_t response;

	if (!starbook_mtl_dma_receipt_provisioned())
		return CB_ERR;
#if ENV_TEST
	response = starbook_mtl_authvar_presence_s3_rearm_trigger_test(
		STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST);
#else
	{
		uint32_t eax = (uint32_t)STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST;
		uint32_t ecx =
			(uint32_t)(STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_REQUEST >> 32);
		const uint16_t dx = pm_acpi_smi_cmd_port();

		__asm__ __volatile__("outb %%al, %%dx"
			: "+a" (eax), "+c" (ecx) : "d" (dx) : "memory");
		response = (uint64_t)eax | ((uint64_t)ecx << 32);
	}
#endif
	return response == STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM_SUCCESS ?
		CB_SUCCESS : CB_ERR;
}

static void starbook_mtl_authvar_presence_s3_resume(void *unused)
{
	uint32_t lifecycle;
	bool acpi_s3;

	(void)unused;
	acpi_s3 = acpi_is_wakeup_s3();
	if (mainboard_loader_instance_authority_lifecycle(&lifecycle) != CB_SUCCESS ||
	    acpi_s3 != (lifecycle == SMM_INVOCATION_LOADER_S3_RELOAD) ||
	    (lifecycle != SMM_INVOCATION_LOADER_NON_S3_LOAD &&
	     lifecycle != SMM_INVOCATION_LOADER_S3_RELOAD))
		die("StarBook MTL loader lifecycle disagrees with ACPI wake state");
	if (acpi_s3 && starbook_mtl_authvar_presence_s3_rearm_send() != CB_SUCCESS)
		die("StarBook MTL authenticated-variable S3 rearm failed");
}

#if ENV_TEST
void starbook_mtl_authvar_presence_s3_resume_test(void)
{
	starbook_mtl_authvar_presence_s3_resume(NULL);
}
#endif

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME_CHECK, BS_ON_ENTRY,
	starbook_mtl_authvar_presence_s3_resume, NULL);
