/* SPDX-License-Identifier: GPL-2.0-only */

#include <drivers/option/cfr_frontend.h>

static const struct sm_object ftpm_recovery = SM_DECLARE_BOOL({
	.flags = CFR_OPTFLAG_LOCK_AT_BOOT,
	.opt_name = "ftpm_recovery",
	.ui_name = "Recover damaged firmware TPM storage",
	.ui_helptext = "On the next boot, allow recovery of damaged firmware TPM storage. "
		"This erases TPM keys and can make encrypted data inaccessible. "
		"Healthy TPM storage is not erased.",
	.default_value = false,
});

static struct sm_obj_form ftpm_form __cfr_form = {
	.ui_name = "Firmware TPM",
	.obj_list = (const struct sm_object *[]) {
		&ftpm_recovery,
		NULL,
	},
};
