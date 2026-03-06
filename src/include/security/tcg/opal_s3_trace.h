/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef SECURITY_TCG_OPAL_S3_TRACE_H
#define SECURITY_TCG_OPAL_S3_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>

#define OPAL_S3_TRACE_SIGNATURE 0x4352544f /* "OTRC" */
#define OPAL_S3_TRACE_VERSION   0x0001
#define OPAL_S3_TRACE_BYTES     512
#define OPAL_S3_TRACE_MAX_ATTEMPTS 10

enum opal_s3_trace_step {
	OPAL_S3_TRACE_STEP_NONE = 0,
	OPAL_S3_TRACE_STEP_ADMIN1_START = 1,
	OPAL_S3_TRACE_STEP_ADMIN1_SET_RANGE = 2,
	OPAL_S3_TRACE_STEP_USER1_START = 3,
	OPAL_S3_TRACE_STEP_USER1_SET_RANGE = 4,
};

struct opal_s3_trace {
	u32 signature;
	u16 version;
	u16 size;

	u32 set_secret_count;
	u32 clear_secret_count;
	u32 set_secret_rc;
	u8 set_bus;
	u8 set_dev;
	u8 set_func;
	u8 set_pw_len;
	u8 set_pw_sum;
	u8 reserved0[3];
	u16 set_comid;
	u16 reserved1;

	u32 arm_count;
	u32 sleep_cycle;
	u32 armed_cycle;
	u8 armed_state;
	u8 state_valid;
	u16 reserved2;

	u32 unlock_invocations;
	u32 unlock_skip_rc;
	u32 unlock_attempts;
	u32 unlock_final_rc;
	u32 unlock_keep_armed;
	u32 last_attempt_index;
	u32 last_attempt_rc;

	u8 final_class;
	u8 final_authority;
	u8 final_stage;
	u8 final_tcg;
	u8 final_method;
	u8 final_policy_step;
	u16 reserved3;
	s32 final_policy_ret;
	u8 final_policy_status;
	u8 reserved4[3];

	s32 admin1_start_ret;
	u8 admin1_start_status;
	u8 reserved5[3];
	s32 admin1_set_ret;
	u8 admin1_set_status;
	u8 reserved6[3];
	s32 user1_start_ret;
	u8 user1_start_status;
	u8 reserved7[3];
	s32 user1_set_ret;
	u8 user1_set_status;
	u8 reserved8[3];

	u32 nvme_init_count;
	u16 nvme_pci_cmd;
	u16 nvme_pmcsr;
	u32 nvme_bar0_low;
	u32 nvme_bar0_high;
	u32 nvme_cc;
	u32 nvme_csts;
	u32 nvme_reinit_cc;
	u32 nvme_reinit_csts;
	u8 nvme_init_result;
	u8 reserved9[3];

	u32 nvme_admin_count;
	u8 nvme_admin_opcode;
	u8 nvme_admin_timeout;
	u16 nvme_admin_status;
	u16 nvme_admin_pci_cmd;
	u16 reserved10;
	u32 nvme_admin_dw10;
	u32 nvme_admin_dw11;
	u32 nvme_admin_cc;
	u32 nvme_admin_csts;

	struct {
		u32 rc;
		u32 nvme_admin_dw10;
		u32 nvme_admin_dw11;
		u16 nvme_admin_status;
		u8 index;
		u8 final_class;
		u8 final_authority;
		u8 final_stage;
		u8 final_tcg;
		u8 final_method;
		u8 final_policy_step;
		u8 nvme_admin_opcode;
		u8 nvme_admin_timeout;
		u8 nvme_init_result;
		u8 reserved11[2];
	} attempts[OPAL_S3_TRACE_MAX_ATTEMPTS];
} __packed;

static inline struct opal_s3_trace *opal_s3_trace_from_scratch(uintptr_t scratch_base,
							 size_t scratch_size)
{
	if (!scratch_base || (scratch_size < OPAL_S3_TRACE_BYTES))
		return NULL;

	return (struct opal_s3_trace *)(uintptr_t)(scratch_base + scratch_size -
						      OPAL_S3_TRACE_BYTES);
}

void opal_s3_trace_reset_for_set_secret(u8 bus, u8 dev, u8 func, u16 comid,
					 u8 pw_len, u8 pw_sum);
void opal_s3_trace_note_set_secret_rc(u32 rc);
void opal_s3_trace_note_clear_secret(void);
void opal_s3_trace_note_arm(u32 sleep_cycle, u32 armed_cycle, u8 armed_state, bool valid);
void opal_s3_trace_note_unlock_entry(u32 sleep_cycle, u32 armed_cycle, u8 armed_state, bool valid);
void opal_s3_trace_note_unlock_skip(u32 rc);
void opal_s3_trace_note_attempt_begin(u32 attempt);
void opal_s3_trace_note_attempt_result(u32 attempt, u32 rc);
void opal_s3_trace_note_unlock_done(u32 rc, bool keep_armed);
void opal_s3_trace_note_policy(u8 step, int ret, u8 method_status);
void opal_s3_trace_note_nvme_init(u16 pci_cmd, u16 pmcsr, u32 bar0_low, u32 bar0_high,
				     u32 cc, u32 csts);
void opal_s3_trace_note_nvme_reinit(bool ok, u32 cc, u32 csts);
void opal_s3_trace_note_nvme_admin(u8 opcode, bool timed_out, u16 status, u16 pci_cmd,
				     u32 dw10, u32 dw11, u32 cc, u32 csts);

#endif
