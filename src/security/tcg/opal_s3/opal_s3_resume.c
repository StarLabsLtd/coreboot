/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <console/console.h>
#include <device/device.h>
#include <security/tcg/opal_s3_resume.h>
#include <smm_call.h>

void opal_s3_resume_unlock(void)
{
	/*
	 * Best-effort: trigger OPAL unlock early on S3 resume.
	 *
	 * For RTD3 storage root ports, the ACPI _ON method also triggers an unlock
	 * SMI once the device is powered. Keep the early resume attempt to cover
	 * platforms/OSes that don't call _ON on resume. The SMM handler will retry
	 * transient init failures and keep the S3 cycle armed when the device
	 * isn't ready yet.
	 */
	u32 rc = call_smm(APM_CNT_OPAL_S3_UNLOCK, 0, NULL);
	if (rc)
		printk(BIOS_DEBUG, "OPAL-S3: resume unlock rc=0x%x\n", rc);
}
