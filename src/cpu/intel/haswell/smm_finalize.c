/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <cpu/x86/msr.h>

#include "haswell.h"

void intel_cpu_haswell_enable_smm_code_access_check(void)
{
	msr_t smm_mca_cap = rdmsr(SMM_MCA_CAP_MSR);
	msr_t smm_feature_control;

	if (!(smm_mca_cap.hi & SMM_CODE_ACCESS_CHK_MASK))
		return;

	smm_feature_control = rdmsr(SMM_FEATURE_CONTROL_MSR);
	if (smm_feature_control.lo & SMM_FEATURE_CONTROL_LOCK) {
		if (!(smm_feature_control.lo & SMM_CODE_CHK_EN))
			printk(BIOS_WARNING,
			       "SMM feature control already locked without SMM code check\n");
		return;
	}

	smm_feature_control.lo |= SMM_CODE_CHK_EN | SMM_FEATURE_CONTROL_LOCK;
	wrmsr(SMM_FEATURE_CONTROL_MSR, smm_feature_control);
}
