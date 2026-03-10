/* SPDX-License-Identifier: GPL-2.0-only */

#include <cbmem.h>
#include <commonlib/bsd/cbmem_id.h>
#include <cpu/x86/smm.h>
#include <console/console.h>
#include <device/device.h>
#include <security/tcg/opal_s3_diag.h>
#include <security/tcg/opal_s3_resume.h>
#include <security/tcg/opal_s3_trace.h>
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

static void opal_s3_resume_dump_trace(void)
{
	void *scratch;
	struct opal_s3_trace *trace;
	u32 i;

	scratch = cbmem_find(CBMEM_ID_OPAL_S3_SCRATCH);
	if (!scratch)
		return;

	trace = opal_s3_trace_from_scratch((uintptr_t)scratch, CONFIG_SMM_OPAL_S3_SCRATCH_SIZE);
	if (!trace || trace->signature != OPAL_S3_TRACE_SIGNATURE ||
	    trace->version != OPAL_S3_TRACE_VERSION || trace->size != sizeof(*trace))
		return;

	printk(BIOS_ERR,
	       "OPAL-S3 trace: set_secret count=%u rc=0x%x clear=%u bus=%u dev=%u func=%u comid=0x%04x pw_len=%u pw_sum=0x%02x\n",
	       trace->set_secret_count, trace->set_secret_rc, trace->clear_secret_count,
	       trace->set_bus, trace->set_dev, trace->set_func, trace->set_comid,
	       trace->set_pw_len, trace->set_pw_sum);
	printk(BIOS_ERR,
	       "OPAL-S3 trace: arm count=%u valid=%u armed=%u sleep_cycle=%u armed_cycle=%u unlocks=%u skip=0x%x attempts=%u last_attempt=%u rc=0x%x final_rc=0x%x keep=%u\n",
	       trace->arm_count, trace->state_valid, trace->armed_state, trace->sleep_cycle,
	       trace->armed_cycle, trace->unlock_invocations, trace->unlock_skip_rc,
	       trace->unlock_attempts, trace->last_attempt_index, trace->last_attempt_rc,
	       trace->unlock_final_rc, trace->unlock_keep_armed);
	printk(BIOS_ERR,
	       "OPAL-S3 trace: policy admin1(start ret=%d status=0x%02x, set ret=%d status=0x%02x) user1(start ret=%d status=0x%02x, set ret=%d status=0x%02x) final(step=%u ret=%d status=0x%02x)\n",
	       trace->admin1_start_ret, trace->admin1_start_status,
	       trace->admin1_set_ret, trace->admin1_set_status,
	       trace->user1_start_ret, trace->user1_start_status,
	       trace->user1_set_ret, trace->user1_set_status,
	       trace->final_policy_step, trace->final_policy_ret, trace->final_policy_status);
	printk(BIOS_ERR,
	       "OPAL-S3 trace: nvme init count=%u pci_cmd=0x%04x pmcsr=0x%04x bar0=%08x:%08x cc=0x%08x csts=0x%08x reinit=%u re_cc=0x%08x re_csts=0x%08x admin count=%u opcode=0x%02x timeout=%u status=0x%04x pci_cmd=0x%04x dw10=0x%08x dw11=0x%08x cc=0x%08x csts=0x%08x\n",
	       trace->nvme_init_count, trace->nvme_pci_cmd, trace->nvme_pmcsr,
	       trace->nvme_bar0_high, trace->nvme_bar0_low, trace->nvme_cc, trace->nvme_csts,
	       trace->nvme_init_result, trace->nvme_reinit_cc, trace->nvme_reinit_csts,
	       trace->nvme_admin_count, trace->nvme_admin_opcode, trace->nvme_admin_timeout,
	       trace->nvme_admin_status, trace->nvme_admin_pci_cmd, trace->nvme_admin_dw10,
	       trace->nvme_admin_dw11, trace->nvme_admin_cc, trace->nvme_admin_csts);
	if (trace->unlock_final_rc && opal_s3_diag_is_encoded(trace->unlock_final_rc)) {
		printk(BIOS_ERR,
		       "OPAL-S3 trace: final class=%u authority=%u stage=0x%x method=0x%02x tcg=%u\n",
		       trace->final_class, trace->final_authority, trace->final_stage,
		       trace->final_method, trace->final_tcg);
	}

	for (i = 0; i < OPAL_S3_TRACE_MAX_ATTEMPTS; i++) {
		const typeof(trace->attempts[0]) *attempt = &trace->attempts[i];

		if (!attempt->index)
			continue;

		printk(BIOS_ERR,
		       "OPAL-S3 trace: attempt %u rc=0x%x class=%u authority=%u stage=0x%x method=0x%02x tcg=%u policy=%u nvme(init=%u opcode=0x%02x timeout=%u status=0x%04x dw10=0x%08x dw11=0x%08x)\n",
		       attempt->index, attempt->rc, attempt->final_class,
		       attempt->final_authority, attempt->final_stage, attempt->final_method,
		       attempt->final_tcg, attempt->final_policy_step,
		       attempt->nvme_init_result, attempt->nvme_admin_opcode,
		       attempt->nvme_admin_timeout, attempt->nvme_admin_status,
		       attempt->nvme_admin_dw10, attempt->nvme_admin_dw11);
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
	opal_s3_resume_dump_trace();
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
