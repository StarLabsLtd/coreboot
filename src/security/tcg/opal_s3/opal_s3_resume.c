/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <security/tcg/opal_s3_resume.h>
#include <smm_call.h>

void opal_s3_resume_unlock(void)
{
	/* Best-effort: attempt OPAL unlock early on S3 resume. */
	(void)call_smm(APM_CNT_OPAL_S3_UNLOCK, 0, NULL);
}
