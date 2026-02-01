/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TCG_OPAL_S3_SMM_H
#define SECURITY_TCG_OPAL_S3_SMM_H

#include <types.h>

/*
 * OPAL S3 SMM helpers.
 *
 * These helpers are called from the default weak mainboard SMI hooks when
 * CONFIG(TCG_OPAL_S3_UNLOCK) is enabled. Boards that override the hooks can
 * call these helpers to integrate OPAL S3 support.
 */
int opal_s3_smi_apmc(u8 apmc);
void opal_s3_smi_sleep(u8 slp_typ);
void opal_s3_smi_sleep_finalize(u8 slp_typ);

#endif
