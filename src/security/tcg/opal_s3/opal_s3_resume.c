/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <halt.h>
#include <security/tcg/opal_s3_resume.h>
#include <smm_call.h>

void opal_s3_resume_unlock(void)
{
	u32 rc = call_smm(APM_CNT_OPAL_S3_UNLOCK, 0, NULL);

	if (CONFIG(DAVE_MODE) && rc)
		die("DAVE: S3 storage admission failed\n");

	if (CONFIG(DEBUG_SMI) && rc)
		printk(BIOS_DEBUG, "OPAL-S3: resume unlock rc=0x%x\n", rc);
}
