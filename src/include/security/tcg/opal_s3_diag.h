/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TCG_OPAL_S3_DIAG_H
#define SECURITY_TCG_OPAL_S3_DIAG_H

#include <types.h>

enum opal_s3_diag_authority {
	OPAL_S3_DIAG_AUTH_NONE = 0,
	OPAL_S3_DIAG_AUTH_ADMIN1 = 1,
	OPAL_S3_DIAG_AUTH_USER1 = 2,
};

enum opal_s3_diag_stage {
	OPAL_S3_DIAG_STAGE_NONE = 0,
	OPAL_S3_DIAG_STAGE_MISSING_PASSWORD = 1,
	OPAL_S3_DIAG_STAGE_INVALID_SCRATCH = 2,
	OPAL_S3_DIAG_STAGE_SCRATCH_OVERLAP = 3,
	OPAL_S3_DIAG_STAGE_SCRATCH_ALIGN = 4,
	OPAL_S3_DIAG_STAGE_NVME_INIT = 5,
	OPAL_S3_DIAG_STAGE_TRUSTED_SEND = 0x10,
	OPAL_S3_DIAG_STAGE_TRUSTED_RECV = 0x11,
	OPAL_S3_DIAG_STAGE_PARSE_INIT = 0x12,
	OPAL_S3_DIAG_STAGE_CHECK_COMID = 0x13,
	OPAL_S3_DIAG_STAGE_GET_METHOD_STATUS = 0x14,
	OPAL_S3_DIAG_STAGE_START_SESSION_CREATE = 0x20,
	OPAL_S3_DIAG_STAGE_START_SESSION_STATUS = 0x21,
	OPAL_S3_DIAG_STAGE_START_SESSION_SYNC = 0x22,
	OPAL_S3_DIAG_STAGE_SET_RANGE_BUILD = 0x30,
	OPAL_S3_DIAG_STAGE_SET_RANGE_STATUS = 0x31,
	OPAL_S3_DIAG_STAGE_END_SESSION_CREATE = 0x40,
	OPAL_S3_DIAG_STAGE_END_SESSION_PARSE = 0x41,
};

#define OPAL_S3_DIAG_RC(_class, _authority, _stage, _method, _ret) \
	((((u32)(_class) & 0xff) << 24) | (((u32)(_authority) & 0x0f) << 20) | \
	 (((u32)(_stage) & 0xff) << 12) | (((u32)(_method) & 0xff) << 4) | \
	 ((u32)(_ret) & 0x0f))

static inline bool opal_s3_diag_is_encoded(u32 rc)
{
	return (rc & 0xff000000U) != 0;
}

static inline u8 opal_s3_diag_class(u32 rc)
{
	return opal_s3_diag_is_encoded(rc) ? ((rc >> 24) & 0xff) : (u8)rc;
}

static inline u8 opal_s3_diag_authority(u32 rc)
{
	return (rc >> 20) & 0x0f;
}

static inline u8 opal_s3_diag_stage(u32 rc)
{
	return (rc >> 12) & 0xff;
}

static inline u8 opal_s3_diag_method_status(u32 rc)
{
	return (rc >> 4) & 0xff;
}

static inline u8 opal_s3_diag_tcg_result(u32 rc)
{
	return rc & 0x0f;
}

#endif
