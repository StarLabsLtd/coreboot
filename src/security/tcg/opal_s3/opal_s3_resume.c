/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <console/console.h>
#include <security/tcg/opal_s3_resume.h>
#include <smm_call.h>

void opal_s3_resume_unlock(void)
{
	/* Best-effort: attempt OPAL unlock early on S3 resume. */
	u32 rc = call_smm(APM_CNT_OPAL_S3_UNLOCK, 0, NULL);
	if (CONFIG(DEBUG_SMI)) {
		/* Keep logs quiet unless explicitly debugging SMIs. */
		printk(BIOS_DEBUG, "OPAL-S3: resume unlock rc=0x%x\n", rc);
	}
}
