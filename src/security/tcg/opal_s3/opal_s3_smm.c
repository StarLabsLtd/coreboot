/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <cpu/x86/save_state.h>
#include <cpu/x86/smm.h>
#include <delay.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
#include <device/pci_type.h>
#include <security/tcg/opal_s3.h>
#include <security/tcg/opal_s3_diag.h>
#include <security/tcg/opal_s3_smm.h>
#include <security/tcg/opal_s3_trace.h>
#include <vendorcode/tcg/opal/opal_unlock.h>
#include <stddef.h>
#include <string.h>
#include <types.h>

#include "opal_secure.h"

#define OPAL_S3_SCRATCH_ALIGN     4096
#define OPAL_S3_SCRATCH_PAGES     3
#define OPAL_S3_SCRATCH_MIN_BYTES (OPAL_S3_SCRATCH_PAGES * OPAL_S3_SCRATCH_ALIGN)

#define OPAL_S3_UNLOCK_INITIAL_DELAY_MS 750
#define OPAL_S3_UNLOCK_RETRY_DELAY_MS   150
#define OPAL_S3_UNLOCK_RETRIES          10

#define OPAL_S3_STATE_SIGNATURE 0x33534c4f /* "OLS3" */
#define OPAL_S3_STATE_VERSION   0x0001

#define OPAL_S3_ARMED_NONE      0
#define OPAL_S3_ARMED_S3        1

struct opal_s3_state {
	u32 signature;
	u16 version;
	u16 size;

	u8 valid;
	u8 armed_state;
	u16 reserved0;

	u32 sleep_cycle;
	u32 armed_cycle;

	u16 base_comid;
	u8 password_len;
	u8 reserved1;
	u8 password[OPAL_S3_MAX_PASSWORD_LEN];

	u8 bus;
	u8 dev;
	u8 func;
	u8 reserved2;

	u32 nvme_bar0_low;
	u32 nvme_bar0_high;
} __packed;

static struct opal_s3_state state_fallback;

static struct opal_s3_state *opal_s3_get_state(void)
{
#if CONFIG(SMM_OPAL_S3_STATE_SMRAM)
	uintptr_t base = 0;
	size_t size = 0;

	smm_get_opal_s3_state_buffer(&base, &size);
	if (base && size >= sizeof(struct opal_s3_state))
		return (struct opal_s3_state *)(uintptr_t)base;
#endif

	return &state_fallback;
}

static struct opal_s3_trace *opal_s3_get_trace(void)
{
	uintptr_t scratch_base = 0;
	size_t scratch_size = 0;

	smm_get_opal_s3_scratch_buffer(&scratch_base, &scratch_size);
	return opal_s3_trace_from_scratch(scratch_base, scratch_size);
}

static u8 opal_s3_pw_sum(const u8 *password, u8 password_len)
{
	u8 sum = 0;

	while (password_len--)
		sum = (u8)(sum + *password++);

	return sum;
}

static void opal_s3_trace_init_if_needed(struct opal_s3_trace *trace)
{
	if (!trace)
		return;

	if ((trace->signature == OPAL_S3_TRACE_SIGNATURE) &&
	    (trace->version == OPAL_S3_TRACE_VERSION) &&
	    (trace->size == sizeof(*trace)))
		return;

	memset(trace, 0, sizeof(*trace));
	trace->signature = OPAL_S3_TRACE_SIGNATURE;
	trace->version = OPAL_S3_TRACE_VERSION;
	trace->size = sizeof(*trace);
}

static void opal_s3_trace_note_attempt_snapshot(struct opal_s3_trace *trace, u32 attempt, u32 rc)
{
	typeof(trace->attempts[0]) *slot;

	if (!trace || !attempt || attempt > OPAL_S3_TRACE_MAX_ATTEMPTS)
		return;

	slot = &trace->attempts[attempt - 1];
	memset(slot, 0, sizeof(*slot));
	slot->index = attempt;
	slot->rc = rc;
	slot->final_class = opal_s3_diag_class(rc);
	slot->final_authority = opal_s3_diag_authority(rc);
	slot->final_stage = opal_s3_diag_stage(rc);
	slot->final_method = opal_s3_diag_method_status(rc);
	slot->final_tcg = opal_s3_diag_tcg_result(rc);
	slot->final_policy_step = trace->final_policy_step;
	slot->nvme_admin_opcode = trace->nvme_admin_opcode;
	slot->nvme_admin_timeout = trace->nvme_admin_timeout;
	slot->nvme_admin_status = trace->nvme_admin_status;
	slot->nvme_admin_dw10 = trace->nvme_admin_dw10;
	slot->nvme_admin_dw11 = trace->nvme_admin_dw11;
	slot->nvme_init_result = trace->nvme_init_result;
}

void opal_s3_trace_reset_for_set_secret(u8 bus, u8 dev, u8 func, u16 comid,
					 u8 pw_len, u8 pw_sum)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();
	u32 set_secret_count = 1;

	if (!trace)
		return;

	if ((trace->signature == OPAL_S3_TRACE_SIGNATURE) &&
	    (trace->version == OPAL_S3_TRACE_VERSION) &&
	    (trace->size == sizeof(*trace)))
		set_secret_count = trace->set_secret_count + 1;

	memset(trace, 0, sizeof(*trace));
	trace->signature = OPAL_S3_TRACE_SIGNATURE;
	trace->version = OPAL_S3_TRACE_VERSION;
	trace->size = sizeof(*trace);
	trace->set_secret_count = set_secret_count;
	trace->set_bus = bus;
	trace->set_dev = dev;
	trace->set_func = func;
	trace->set_comid = comid;
	trace->set_pw_len = pw_len;
	trace->set_pw_sum = pw_sum;
}

void opal_s3_trace_note_set_secret_rc(u32 rc)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->set_secret_rc = rc;
}

void opal_s3_trace_note_clear_secret(void)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->clear_secret_count++;
}

void opal_s3_trace_note_arm(u32 sleep_cycle, u32 armed_cycle, u8 armed_state, bool valid)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->arm_count++;
	trace->sleep_cycle = sleep_cycle;
	trace->armed_cycle = armed_cycle;
	trace->armed_state = armed_state;
	trace->state_valid = valid;
}

void opal_s3_trace_note_unlock_entry(u32 sleep_cycle, u32 armed_cycle, u8 armed_state, bool valid)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->unlock_invocations++;
	trace->unlock_skip_rc = 0;
	trace->unlock_attempts = 0;
	trace->unlock_final_rc = 0;
	trace->unlock_keep_armed = 0;
	trace->last_attempt_index = 0;
	trace->last_attempt_rc = 0;
	trace->final_class = 0;
	trace->final_authority = 0;
	trace->final_stage = 0;
	trace->final_tcg = 0;
	trace->final_method = 0;
	trace->final_policy_step = OPAL_S3_TRACE_STEP_NONE;
	trace->final_policy_ret = 0;
	trace->final_policy_status = 0;
	trace->admin1_start_ret = 0;
	trace->admin1_start_status = 0;
	trace->admin1_set_ret = 0;
	trace->admin1_set_status = 0;
	trace->user1_start_ret = 0;
	trace->user1_start_status = 0;
	trace->user1_set_ret = 0;
	trace->user1_set_status = 0;
	trace->nvme_init_count = 0;
	trace->nvme_pci_cmd = 0;
	trace->nvme_pmcsr = 0;
	trace->nvme_bar0_low = 0;
	trace->nvme_bar0_high = 0;
	trace->nvme_cc = 0;
	trace->nvme_csts = 0;
	trace->nvme_reinit_cc = 0;
	trace->nvme_reinit_csts = 0;
	trace->nvme_init_result = 0;
	trace->nvme_admin_count = 0;
	trace->nvme_admin_opcode = 0;
	trace->nvme_admin_timeout = 0;
	trace->nvme_admin_status = 0;
	trace->nvme_admin_pci_cmd = 0;
	trace->nvme_admin_dw10 = 0;
	trace->nvme_admin_dw11 = 0;
	trace->nvme_admin_cc = 0;
	trace->nvme_admin_csts = 0;
	memset(trace->attempts, 0, sizeof(trace->attempts));
	trace->sleep_cycle = sleep_cycle;
	trace->armed_cycle = armed_cycle;
	trace->armed_state = armed_state;
	trace->state_valid = valid;
}

void opal_s3_trace_note_unlock_skip(u32 rc)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->unlock_skip_rc = rc;
	trace->unlock_final_rc = rc;
}

void opal_s3_trace_note_attempt_begin(u32 attempt)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->unlock_attempts = attempt;
	trace->last_attempt_index = attempt;
}

void opal_s3_trace_note_attempt_result(u32 attempt, u32 rc)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->last_attempt_index = attempt;
	trace->last_attempt_rc = rc;
	opal_s3_trace_note_attempt_snapshot(trace, attempt, rc);
}

void opal_s3_trace_note_unlock_done(u32 rc, bool keep_armed)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->unlock_final_rc = rc;
	trace->unlock_keep_armed = keep_armed ? 1 : 0;
	if (opal_s3_diag_is_encoded(rc)) {
		trace->final_class = opal_s3_diag_class(rc);
		trace->final_authority = opal_s3_diag_authority(rc);
		trace->final_stage = opal_s3_diag_stage(rc);
		trace->final_method = opal_s3_diag_method_status(rc);
		trace->final_tcg = opal_s3_diag_tcg_result(rc);
	}
}

void opal_s3_trace_note_policy(u8 step, int ret, u8 method_status)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->final_policy_step = step;
	trace->final_policy_ret = ret;
	trace->final_policy_status = method_status;

	switch (step) {
	case OPAL_S3_TRACE_STEP_ADMIN1_START:
		trace->admin1_start_ret = ret;
		trace->admin1_start_status = method_status;
		break;
	case OPAL_S3_TRACE_STEP_ADMIN1_SET_RANGE:
		trace->admin1_set_ret = ret;
		trace->admin1_set_status = method_status;
		break;
	case OPAL_S3_TRACE_STEP_USER1_START:
		trace->user1_start_ret = ret;
		trace->user1_start_status = method_status;
		break;
	case OPAL_S3_TRACE_STEP_USER1_SET_RANGE:
		trace->user1_set_ret = ret;
		trace->user1_set_status = method_status;
		break;
	default:
		break;
	}
}

void opal_s3_trace_note_nvme_init(u16 pci_cmd, u16 pmcsr, u32 bar0_low, u32 bar0_high,
				     u32 cc, u32 csts)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->nvme_init_count++;
	trace->nvme_pci_cmd = pci_cmd;
	trace->nvme_pmcsr = pmcsr;
	trace->nvme_bar0_low = bar0_low;
	trace->nvme_bar0_high = bar0_high;
	trace->nvme_cc = cc;
	trace->nvme_csts = csts;
}

void opal_s3_trace_note_nvme_reinit(bool ok, u32 cc, u32 csts)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->nvme_init_result = ok ? 1 : 2;
	trace->nvme_reinit_cc = cc;
	trace->nvme_reinit_csts = csts;
}

void opal_s3_trace_note_nvme_admin(u8 opcode, bool timed_out, u16 status, u16 pci_cmd,
				     u32 dw10, u32 dw11, u32 cc, u32 csts)
{
	struct opal_s3_trace *trace = opal_s3_get_trace();

	opal_s3_trace_init_if_needed(trace);
	if (!trace)
		return;

	trace->nvme_admin_count++;
	trace->nvme_admin_opcode = opcode;
	trace->nvme_admin_timeout = timed_out ? 1 : 0;
	trace->nvme_admin_status = status;
	trace->nvme_admin_pci_cmd = pci_cmd;
	trace->nvme_admin_dw10 = dw10;
	trace->nvme_admin_dw11 = dw11;
	trace->nvme_admin_cc = cc;
	trace->nvme_admin_csts = csts;
}

static bool opal_s3_validate_scratch(uintptr_t base, size_t size, uintptr_t *aligned_base_out,
				     size_t *aligned_size_out)
{
	uintptr_t aligned;
	size_t aligned_size;

	if (!base || !size)
		return false;

	if (size <= OPAL_S3_TRACE_BYTES)
		return false;

	size -= OPAL_S3_TRACE_BYTES;

	if (base + size < base)
		return false;

	aligned = ALIGN_UP(base, OPAL_S3_SCRATCH_ALIGN);
	if (aligned < base)
		return false;

	if (aligned - base > size)
		return false;

	aligned_size = size - (aligned - base);
	if (aligned_size < OPAL_S3_SCRATCH_MIN_BYTES)
		return false;

	if ((aligned & (OPAL_S3_SCRATCH_ALIGN - 1)) != 0)
		return false;

	/* Scratch must never overlap SMRAM. */
	if (smm_points_to_smram((void *)(uintptr_t)aligned, OPAL_S3_SCRATCH_MIN_BYTES))
		return false;

	/* Prove the aligned scratch range stays inside the reserved CBMEM entry. */
	if (aligned + OPAL_S3_SCRATCH_MIN_BYTES < aligned)
		return false;
	if (aligned + OPAL_S3_SCRATCH_MIN_BYTES > base + size)
		return false;

	if (CONFIG(DEBUG_SMI)) {
		printk(BIOS_DEBUG, "OPAL: scratch CBMEM [0x%lx+0x%zx] -> [0x%lx+0x%zx]\n",
		       (unsigned long)base, size, (unsigned long)aligned, aligned_size);
	}

	*aligned_base_out = aligned;
	*aligned_size_out = aligned_size;
	return true;
}

static u32 opal_s3_clear_secret(void)
{
	struct opal_s3_state *st = opal_s3_get_state();
	opal_explicit_bzero(st, sizeof(*st));
	opal_s3_trace_note_clear_secret();

	return 0;
}

static u32 opal_s3_set_secret(const struct opal_s3_smm_ctx *ctx)
{
	struct opal_s3_state *st = opal_s3_get_state();
	uintptr_t scratch_base = 0;
	size_t scratch_size = 0;
	uintptr_t aligned_base = 0;
	size_t aligned_size = 0;
	pci_devfn_t nvme_dev;

	opal_s3_trace_reset_for_set_secret(ctx ? ctx->bus : 0, ctx ? ctx->dev : 0,
					     ctx ? ctx->func : 0, ctx ? ctx->base_comid : 0,
					     ctx ? ctx->password_len : 0,
					     ctx ? opal_s3_pw_sum(ctx->password, ctx->password_len) : 0);

	/* Fail closed: clear any prior state for any SET_SECRET attempt. */
	opal_s3_clear_secret();

	if (!ctx) {
		opal_s3_trace_note_set_secret_rc(1);
		return 1;
	}

	/* Never dereference pointers into SMRAM. */
	if (smm_points_to_smram(ctx, sizeof(*ctx))) {
		opal_s3_trace_note_set_secret_rc(2);
		return 2;
	}

	if (ctx->signature != OPAL_S3_SMM_CTX_SIGNATURE) {
		opal_s3_trace_note_set_secret_rc(3);
		return 3;
	}

	if (ctx->version != OPAL_S3_SMM_CTX_VERSION) {
		opal_s3_trace_note_set_secret_rc(4);
		return 4;
	}

	if (ctx->size < sizeof(*ctx)) {
		opal_s3_trace_note_set_secret_rc(5);
		return 5;
	}

	if (ctx->password_len == 0 || ctx->password_len > OPAL_S3_MAX_PASSWORD_LEN) {
		opal_s3_trace_note_set_secret_rc(6);
		return 6;
	}

	/*
	 * The NVMe scratch buffer must live in a coreboot-owned reserved region
	 * that persists across S3. Using CBMEM makes this placement provable:
	 * CBMEM is reserved from the OS via the memory map, and we can validate
	 * that the buffer range never overlaps SMRAM.
	 */
	smm_get_opal_s3_scratch_buffer(&scratch_base, &scratch_size);
		if (!opal_s3_validate_scratch(scratch_base, scratch_size, &aligned_base,
					      &aligned_size)) {
			printk(BIOS_ERR, "OPAL: invalid scratch (base=0x%lx size=0x%zx)\n",
			       (unsigned long)scratch_base, scratch_size);
			opal_s3_trace_note_set_secret_rc(7);
			return 7;
		}

	nvme_dev = PCI_DEV(ctx->bus, ctx->dev, ctx->func);

	st->signature = OPAL_S3_STATE_SIGNATURE;
	st->version = OPAL_S3_STATE_VERSION;
	st->size = sizeof(*st);
	st->valid = 1;
	st->armed_state = OPAL_S3_ARMED_NONE;
	st->sleep_cycle = 0;
	st->armed_cycle = 0;

	st->bus = ctx->bus;
	st->dev = ctx->dev;
	st->func = ctx->func;
	st->base_comid = ctx->base_comid;
	st->password_len = ctx->password_len;
	memcpy(st->password, ctx->password, ctx->password_len);

	/*
	 * Cache BAR0 so S3 resume can restore it if the device loses PCI config
	 * state across sleep (common on some platforms). Store raw BAR dwords
	 * including attribute bits.
	 */
	st->nvme_bar0_low = pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0);
	st->nvme_bar0_high = pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0 + 4);

	printk(BIOS_INFO,
	       "OPAL: set secret ok for %02x:%02x.%x base_comid=0x%04x pw_len=%u bar0=%08x:%08x scratch=0x%lx+0x%zx\n",
	       ctx->bus, ctx->dev, ctx->func, st->base_comid, st->password_len,
	       st->nvme_bar0_high, st->nvme_bar0_low, (unsigned long)aligned_base, aligned_size);
	opal_s3_trace_note_set_secret_rc(0);

	return 0;
}

static void opal_s3_arm_for_s3(void)
{
	struct opal_s3_state *st = opal_s3_get_state();

	if (st->signature != OPAL_S3_STATE_SIGNATURE || st->version != OPAL_S3_STATE_VERSION ||
	    st->size != sizeof(*st) || !st->valid)
		return;

	/* Rate-limit: once per sleep entry. */
	if (st->armed_state == OPAL_S3_ARMED_S3)
		return;

	st->sleep_cycle++;
	st->armed_cycle = st->sleep_cycle;
	st->armed_state = OPAL_S3_ARMED_S3;
	opal_s3_trace_note_arm(st->sleep_cycle, st->armed_cycle, st->armed_state, st->valid != 0);

	printk(BIOS_INFO, "OPAL: armed for S3 (cycle=%u)\n", st->sleep_cycle);
}

static void opal_s3_restore_nvme_bar0_if_needed(pci_devfn_t nvme_dev, u32 saved_bar0_low,
						u32 saved_bar0_high)
{
	const u32 cur_low = pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0);
	const u32 cur_base = cur_low & ~PCI_BASE_ADDRESS_MEM_ATTR_MASK;
	const u32 saved_base = saved_bar0_low & ~PCI_BASE_ADDRESS_MEM_ATTR_MASK;
	const bool is_saved_mem =
		((saved_bar0_low & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY);
	const bool is_saved_64 = ((saved_bar0_low & PCI_BASE_ADDRESS_MEM_LIMIT_MASK) ==
				  PCI_BASE_ADDRESS_MEM_LIMIT_64);

	if (cur_low != 0xffffffff && cur_base != 0)
		return;

	if (!saved_bar0_low || saved_base == 0 || !is_saved_mem)
		return;

	pci_write_config32(nvme_dev, PCI_BASE_ADDRESS_0, saved_bar0_low);
	if (is_saved_64)
		pci_write_config32(nvme_dev, PCI_BASE_ADDRESS_0 + 4, saved_bar0_high);

	if (CONFIG(DEBUG_SMI)) {
		const u32 new_low = pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0);
		printk(BIOS_DEBUG, "OPAL: restored NVMe BAR0 0x%08x -> 0x%08x\n", cur_low,
		       new_low);
	}
}

static u32 opal_s3_unlock(const struct opal_s3_state *st)
{
	uintptr_t scratch_base = 0;
	size_t scratch_size = 0;
	uintptr_t aligned_base = 0;
	size_t aligned_size = 0;
	pci_devfn_t nvme_dev;

	smm_get_opal_s3_scratch_buffer(&scratch_base, &scratch_size);
	if (!opal_s3_validate_scratch(scratch_base, scratch_size, &aligned_base,
				      &aligned_size)) {
		printk(BIOS_ERR, "OPAL: scratch invariant failed at unlock\n");
		return 1;
	}

	nvme_dev = PCI_DEV(st->bus, st->dev, st->func);

	opal_s3_restore_nvme_bar0_if_needed(nvme_dev, st->nvme_bar0_low, st->nvme_bar0_high);

	return opal_nvme_opal_unlock(nvme_dev, st->base_comid, st->password, st->password_len,
				     (void *)(uintptr_t)aligned_base, aligned_size);
}

static bool opal_s3_unlock_retryable(u32 rc)
{
	if (!opal_s3_diag_is_encoded(rc))
		return false;

	/*
	 * NVMe init failures are expected when resume sequencing is still
	 * catching up. Also retry trusted send/recv transport failures: on
	 * some platforms the controller is enumerated but not yet ready to
	 * accept OPAL Security Send/Receive commands on the first S3 attempt.
	 */
	if (opal_s3_diag_class(rc) == 1)
		return true;

	if (opal_s3_diag_class(rc) != 3)
		return false;

	switch (opal_s3_diag_stage(rc)) {
	case OPAL_S3_DIAG_STAGE_TRUSTED_SEND:
	case OPAL_S3_DIAG_STAGE_TRUSTED_RECV:
		return true;
	default:
		return false;
	}
}

static bool opal_s3_unlock_keep_armed(u32 rc)
{
	if (!opal_s3_diag_is_encoded(rc)) {
		/*
		 * The 26.02 transport returns plain integer failures. Preserve
		 * the S3 armed state for those so the later RTD3 _ON-triggered
		 * unlock gets a chance to retry in the same resume cycle.
		 */
		switch (rc) {
		case 1:
		case 3:
			return true;
		default:
			return false;
		}
	}

	/*
	 * Preserve the S3 armed state for both "device not ready yet" failures
	 * and OPAL-stack failures. This matches the more permissive resume
	 * behavior that allowed a later RTD3-triggered unlock to recover.
	 */
	switch (opal_s3_diag_class(rc)) {
	case 1:
	case 3:
		return true;
	default:
		return false;
	}
}

static u32 opal_s3_unlock_if_armed(void)
{
	struct opal_s3_state *st = opal_s3_get_state();
	u32 rc;
	bool keep_armed;

	opal_s3_trace_note_unlock_entry(st->sleep_cycle, st->armed_cycle, st->armed_state,
					 st->valid != 0);

	if (st->signature != OPAL_S3_STATE_SIGNATURE || st->version != OPAL_S3_STATE_VERSION ||
	    st->size != sizeof(*st) || !st->valid) {
		printk(BIOS_INFO, "OPAL: unlock skipped (no secret)\n");
		opal_s3_trace_note_unlock_skip(0x10);
		return 0x10;
	}

	if (st->armed_state == OPAL_S3_ARMED_NONE) {
		printk(BIOS_INFO, "OPAL: unlock skipped (not armed)\n");
		opal_s3_trace_note_unlock_skip(0x11);
		return 0x11;
	}

	if (st->armed_cycle != st->sleep_cycle) {
		printk(BIOS_ERR, "OPAL: unlock rejected (invalid sequence)\n");
		opal_s3_clear_secret();
		opal_s3_trace_note_unlock_skip(0x12);
		return 0x12;
	}

	/*
	 * Give the NVMe controller and upstream PCIe fabric a moment to come
	 * back after S3 before attempting MMIO + DMA.
	 */
	printk(BIOS_INFO,
	       "OPAL: unlock start bus=%02x dev=%02x func=%x cycle=%u initial_delay=%u retry_delay=%u retries=%u\n",
	       st->bus, st->dev, st->func, st->sleep_cycle, OPAL_S3_UNLOCK_INITIAL_DELAY_MS,
	       OPAL_S3_UNLOCK_RETRY_DELAY_MS, OPAL_S3_UNLOCK_RETRIES);
	mdelay(OPAL_S3_UNLOCK_INITIAL_DELAY_MS);

	rc = 1;
	for (int attempt = 0; attempt < OPAL_S3_UNLOCK_RETRIES; attempt++) {
		opal_s3_trace_note_attempt_begin(attempt + 1);
		printk(BIOS_INFO, "OPAL: unlock attempt %d/%d\n", attempt + 1,
		       OPAL_S3_UNLOCK_RETRIES);

		if (attempt)
			mdelay(OPAL_S3_UNLOCK_RETRY_DELAY_MS);

		rc = opal_s3_unlock(st);
		opal_s3_trace_note_attempt_result(attempt + 1, rc);
		if (rc && opal_s3_diag_is_encoded(rc)) {
			printk(BIOS_INFO,
			       "OPAL: unlock attempt %d rc=0x%x class=%u stage=0x%x method=0x%x tcg=%u\n",
			       attempt + 1, rc, opal_s3_diag_class(rc), opal_s3_diag_stage(rc),
			       opal_s3_diag_method_status(rc), opal_s3_diag_tcg_result(rc));
		}

		if (rc == 0)
			break;

		if (!opal_s3_unlock_retryable(rc))
			break;
	}
	if (rc)
		printk(BIOS_ERR, "OPAL: unlock failed (rc=%u)\n", rc);
	else
		printk(BIOS_INFO, "OPAL: unlock succeeded\n");

	/*
	 * Keep the S3 cycle armed on retryable/not-ready cases and OPAL-stack
	 * failures so a later resume-time trigger (e.g. from an RTD3 _ON
	 * method) can retry.
	 */
	keep_armed = opal_s3_unlock_keep_armed(rc);
	opal_s3_trace_note_unlock_done(rc, keep_armed);
	if (!keep_armed) {
		st->armed_state = OPAL_S3_ARMED_NONE;
		st->armed_cycle = 0;
		if (rc)
			printk(BIOS_INFO, "OPAL: disarmed after rc=0x%x\n", rc);
	} else if (rc) {
		printk(BIOS_INFO, "OPAL: keeping S3 armed after rc=0x%x\n", rc);
	}

	/*
	 * Keep the secret cached so subsequent S3 cycles can unlock even if the
	 * payload does not run again. Clear it only via the explicit service
	 * command or on non-S3 sleep types.
	 */

	return rc;
}

int opal_s3_smi_apmc(u8 apmc)
{
	int node;
	u64 rax;
	u64 rbx;
	u8 subcmd;
	u32 ret;
	u32 unlock_rc;

	/*
	 * Reduce attack surface: only handle the OPAL service command and the
	 * resume unlock trigger. Ignore everything else.
	 */
	switch (apmc) {
	case APM_CNT_OPAL_S3_UNLOCK:
		/*
		 * Write a small status code back into RAX so the caller can log
		 * whether unlock was attempted/succeeded without relying on SMM logs.
		 */
		unlock_rc = opal_s3_unlock_if_armed();
		printk(BIOS_DEBUG, "OPAL-S3: unlock SMI rc=0x%x\n", unlock_rc);

		node = get_apmc_node(apmc);
		if (node >= 0) {
			const u64 rax_out = unlock_rc;
			(void)set_save_state_reg(RAX, node, (void *)&rax_out, sizeof(rax_out));
		}
		return 1;

	case APM_CNT_OPAL_SVC:
		break;

	default:
		return 0;
	}

	node = get_apmc_node(apmc);
	if (node < 0)
		return 0;

	if (get_save_state_reg(RAX, node, &rax, sizeof(rax)) < 0)
		return 0;
	if (get_save_state_reg(RBX, node, &rbx, sizeof(rbx)) < 0)
		return 0;

	subcmd = (rax >> 8) & 0xff;

	switch (subcmd) {
	case OPAL_SMM_SUBCMD_SET_SECRET:
		ret = opal_s3_set_secret((const struct opal_s3_smm_ctx *)(uintptr_t)rbx);
		printk(BIOS_INFO, "OPAL: SMI SET_SECRET rc=0x%x\n", ret);
		break;
	case OPAL_SMM_SUBCMD_CLEAR_SECRET:
		ret = opal_s3_clear_secret();
		printk(BIOS_INFO, "OPAL: SMI CLEAR_SECRET rc=0x%x\n", ret);
		break;
	default:
		ret = 0xfffffffe;
		break;
	}

	{
		const u64 rax_out = ret;
		(void)set_save_state_reg(RAX, node, (void *)&rax_out, sizeof(rax_out));
	}
	return 1;
}

void opal_s3_smi_sleep(u8 slp_typ)
{
	/* Only keep secrets for a clean S3 sleep cycle. */
	if (slp_typ == ACPI_S3)
		opal_s3_arm_for_s3();
	else
		opal_s3_clear_secret();
}

void opal_s3_smi_sleep_finalize(u8 slp_typ)
{
	/* Keep secrets only for the next S3 resume/unlock attempt. */
	if (slp_typ != ACPI_S3)
		opal_s3_clear_secret();
}
