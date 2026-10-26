/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TCG_OPAL_S3_H
#define SECURITY_TCG_OPAL_S3_H

#include <stddef.h>
#include <stdint.h>

/*
 * ABI between payload software and the SMM OPAL S3 unlock handler.
 *
 * The payload is expected to place this structure in normal RAM and pass its
 * address in RBX when calling call_smm(APM_CNT_OPAL_SVC, OPAL_SMM_SUBCMD_*, ctx).
 */

#define OPAL_SMM_SUBCMD_SET_SECRET   0x01
#define OPAL_SMM_SUBCMD_CLEAR_SECRET 0x02
#define OPAL_SMM_SUBCMD_CLEAR_DEVICE 0x03

/* CLEAR_DEVICE uses the same context with only its header, BDF and ComID set. */

#define OPAL_S3_SMM_CTX_SIGNATURE 0x3353504f /* "OPS3" */
#define OPAL_S3_SMM_CTX_VERSION   0x0001
/*
 * Fixed size for a stable payload<->SMM ABI.
 *
 * Keep this as a constant (not a Kconfig option): changing it requires a
 * coordinated payload update.
 */
#define OPAL_S3_MAX_PASSWORD_LEN 32
#define OPAL_S3_MAX_SECRETS 8

struct opal_s3_smm_ctx {
	uint32_t signature;
	uint16_t version;
	uint16_t size;

	uint8_t bus;
	uint8_t dev;
	uint8_t func;
	uint8_t reserved0;

	uint16_t base_comid;
	uint16_t reserved1;

	uint8_t password_len;
	uint8_t reserved2[3];
	uint8_t password[OPAL_S3_MAX_PASSWORD_LEN];
} __attribute__((packed));

_Static_assert(sizeof(struct opal_s3_smm_ctx) == 52, "OPAL S3 version 1 wire size");

#endif
