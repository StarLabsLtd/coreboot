/* SPDX-License-Identifier: GPL-2.0-only */

#include <cpu/x86/smm.h>
#include <console/console.h>
#include <device/device.h>
#include <security/tcg/opal_s3_diag.h>
#include <security/tcg/opal_s3_resume.h>
#include <smm_call.h>

static const char *opal_s3_diag_authority_string(u8 authority)
{
	switch (authority) {
	case OPAL_S3_DIAG_AUTH_ADMIN1:
		return "Admin1";
	case OPAL_S3_DIAG_AUTH_USER1:
		return "User1";
	default:
		return "None";
	}
}

static const char *opal_s3_diag_stage_string(u8 stage)
{
	switch (stage) {
	case OPAL_S3_DIAG_STAGE_NONE:
		return "None";
	case OPAL_S3_DIAG_STAGE_MISSING_PASSWORD:
		return "MissingPassword";
	case OPAL_S3_DIAG_STAGE_INVALID_SCRATCH:
		return "InvalidScratch";
	case OPAL_S3_DIAG_STAGE_SCRATCH_OVERLAP:
		return "ScratchOverlapsSmram";
	case OPAL_S3_DIAG_STAGE_SCRATCH_ALIGN:
		return "ScratchAlign";
	case OPAL_S3_DIAG_STAGE_NVME_INIT:
		return "NvmeInit";
	case OPAL_S3_DIAG_STAGE_TRUSTED_SEND:
		return "TrustedSend";
	case OPAL_S3_DIAG_STAGE_TRUSTED_RECV:
		return "TrustedRecv";
	case OPAL_S3_DIAG_STAGE_PARSE_INIT:
		return "ParseInit";
	case OPAL_S3_DIAG_STAGE_CHECK_COMID:
		return "CheckComId";
	case OPAL_S3_DIAG_STAGE_GET_METHOD_STATUS:
		return "GetMethodStatus";
	case OPAL_S3_DIAG_STAGE_START_SESSION_CREATE:
		return "StartSessionCreate";
	case OPAL_S3_DIAG_STAGE_START_SESSION_STATUS:
		return "StartSessionStatus";
	case OPAL_S3_DIAG_STAGE_START_SESSION_SYNC:
		return "StartSessionSync";
	case OPAL_S3_DIAG_STAGE_SET_RANGE_BUILD:
		return "SetRangeBuild";
	case OPAL_S3_DIAG_STAGE_SET_RANGE_STATUS:
		return "SetRangeStatus";
	case OPAL_S3_DIAG_STAGE_END_SESSION_CREATE:
		return "EndSessionCreate";
	case OPAL_S3_DIAG_STAGE_END_SESSION_PARSE:
		return "EndSessionParse";
	default:
		return "Unknown";
	}
}

static const char *opal_s3_diag_tcg_result_string(u8 ret)
{
	switch (ret) {
	case 0:
		return "Success";
	case 1:
		return "Failure";
	case 2:
		return "FailureNullPointer";
	case 3:
		return "FailureZeroSize";
	case 4:
		return "FailureInvalidAction";
	case 5:
		return "FailureBufferTooSmall";
	case 6:
		return "FailureEndBuffer";
	case 7:
		return "FailureInvalidType";
	default:
		return "Unknown";
	}
}

static const char *opal_s3_diag_method_status_string(u8 status)
{
	switch (status) {
	case 0x00:
		return "SUCCESS";
	case 0x01:
		return "NOT_AUTHORIZED";
	case 0x02:
		return "OBSOLETE";
	case 0x03:
		return "SP_BUSY";
	case 0x04:
		return "SP_FAILED";
	case 0x05:
		return "SP_DISABLED";
	case 0x06:
		return "SP_FROZEN";
	case 0x07:
		return "NO_SESSIONS_AVAILABLE";
	case 0x08:
		return "UNIQUENESS_CONFLICT";
	case 0x09:
		return "INSUFFICIENT_SPACE";
	case 0x0a:
		return "INSUFFICIENT_ROWS";
	case 0x0c:
		return "INVALID_PARAMETER";
	case 0x0d:
		return "OBSOLETE2";
	case 0x0e:
		return "OBSOLETE3";
	case 0x0f:
		return "TPER_MALFUNCTION";
	case 0x10:
		return "TRANSACTION_FAILURE";
	case 0x11:
		return "RESPONSE_OVERFLOW";
	case 0x12:
		return "AUTHORITY_LOCKED_OUT";
	case 0x3f:
		return "FAIL";
	default:
		return "Unknown";
	}
}

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
	if (opal_s3_diag_is_encoded(rc)) {
		const u8 authority = opal_s3_diag_authority(rc);
		const u8 stage = opal_s3_diag_stage(rc);
		const u8 method_status = opal_s3_diag_method_status(rc);
		const u8 tcg_ret = opal_s3_diag_tcg_result(rc);

		printk(BIOS_ERR,
		       "OPAL-S3: resume unlock rc=0x%x class=%u authority=%s stage=%s method=0x%02x(%s) tcg=%u(%s)\n",
		       rc, opal_s3_diag_class(rc), opal_s3_diag_authority_string(authority),
		       opal_s3_diag_stage_string(stage), method_status,
		       opal_s3_diag_method_status_string(method_status), tcg_ret,
		       opal_s3_diag_tcg_result_string(tcg_ret));
	} else if (rc) {
		printk(BIOS_DEBUG, "OPAL-S3: resume unlock rc=0x%x\n", rc);
	}
}
