/* SPDX-License-Identifier: GPL-2.0-only */

#include <types.h>
#include <console/console.h>
#include <cpu/x86/msr.h>

#include "haswell.h"

static void enable_smm_code_access_check(void)
{
	msr_t smm_mca_cap = rdmsr(SMM_MCA_CAP_MSR);
	if (!(smm_mca_cap.hi & SMM_CODE_ACCESS_CHK_MASK))
		return;

	/* Despite the prolific EDK2 implementation of this feature
	   performing it on all cores, this is still a package-scoped MSR,
	   per the Haswell BWG, and tested on a later platform. */
	msr_t smm_feature_control = rdmsr(SMM_FEATURE_CONTROL_MSR);
	smm_feature_control.lo |= SMM_CODE_CHK_EN | SMM_FEATURE_CONTROL_LOCK;
	wrmsr(SMM_FEATURE_CONTROL_MSR, smm_feature_control);
}

void intel_cpu_haswell_finalize_smm(void)
{
	enable_smm_code_access_check();

	/* Lock memory configuration to protect SMM */
	msr_set(MSR_LT_LOCK_MEMORY, BIT(0));
}
