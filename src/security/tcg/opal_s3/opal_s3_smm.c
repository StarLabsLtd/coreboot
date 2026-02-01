/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <cpu/x86/save_state.h>
#include <cpu/x86/smm.h>
#include <device/pci_def.h>
#include <device/pci_type.h>
#include <security/tcg/opal_s3.h>
#include <security/tcg/opal_s3_smm.h>
#include <vendorcode/tcg/opal/opal_unlock.h>
#include <stddef.h>
#include <string.h>
#include <types.h>

#include "opal_secure.h"

#define OPAL_S3_SCRATCH_ALIGN     4096
#define OPAL_S3_SCRATCH_PAGES     3
#define OPAL_S3_SCRATCH_MIN_BYTES (OPAL_S3_SCRATCH_PAGES * OPAL_S3_SCRATCH_ALIGN)

struct opal_s3_secret {
	bool valid;
	bool armed;
	u32 sleep_cycle;
	u32 armed_cycle;
	u16 base_comid;
	u8 password_len;
	u8 password[OPAL_S3_MAX_PASSWORD_LEN];

	pci_devfn_t nvme_dev;
	uintptr_t scratch_cbmem_base;
	size_t scratch_cbmem_size;
	void *scratch;
	size_t scratch_size;
};

static struct opal_s3_secret secret;

static bool opal_s3_validate_scratch(uintptr_t base, size_t size, uintptr_t *aligned_base_out,
				     size_t *aligned_size_out)
{
	uintptr_t aligned;
	size_t aligned_size;

	if (!base || !size)
		return false;

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
	opal_explicit_bzero(&secret, sizeof(secret));
	return 0;
}

static u32 opal_s3_set_secret(const struct opal_s3_smm_ctx *ctx)
{
	uintptr_t scratch_base = 0;
	size_t scratch_size = 0;
	uintptr_t aligned_base = 0;
	size_t aligned_size = 0;

	/* Fail closed: clear any prior state for any SET_SECRET attempt. */
	opal_s3_clear_secret();

	if (!ctx)
		return 1;

	/* Never dereference pointers into SMRAM. */
	if (smm_points_to_smram(ctx, sizeof(*ctx)))
		return 2;

	if (ctx->signature != OPAL_S3_SMM_CTX_SIGNATURE)
		return 3;

	if (ctx->version != OPAL_S3_SMM_CTX_VERSION)
		return 4;

	if (ctx->size < sizeof(*ctx))
		return 5;

	if (ctx->password_len == 0 || ctx->password_len > OPAL_S3_MAX_PASSWORD_LEN)
		return 6;

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
		return 7;
	}

	secret.nvme_dev = PCI_DEV(ctx->bus, ctx->dev, ctx->func);
	secret.base_comid = ctx->base_comid;
	secret.password_len = ctx->password_len;
	memcpy(secret.password, ctx->password, ctx->password_len);

	secret.scratch_cbmem_base = scratch_base;
	secret.scratch_cbmem_size = scratch_size;
	secret.scratch = (void *)(uintptr_t)aligned_base;
	secret.scratch_size = aligned_size;

	secret.valid = true;
	secret.armed = false;
	secret.armed_cycle = 0;

	return 0;
}

static void opal_s3_arm_for_s3(void)
{
	if (!secret.valid)
		return;

	/* Rate-limit: once per sleep entry. */
	if (secret.armed)
		return;

	secret.sleep_cycle++;
	secret.armed_cycle = secret.sleep_cycle;
	secret.armed = true;
}

static u32 opal_s3_unlock(void)
{
	uintptr_t aligned_base = 0;
	size_t aligned_size = 0;

	if (!opal_s3_validate_scratch(secret.scratch_cbmem_base, secret.scratch_cbmem_size,
				      &aligned_base, &aligned_size)) {
		printk(BIOS_ERR, "OPAL: scratch invariant failed at unlock\n");
		return 1;
	}

	secret.scratch = (void *)(uintptr_t)aligned_base;
	secret.scratch_size = aligned_size;

	return opal_nvme_opal_unlock(secret.nvme_dev, secret.base_comid, secret.password,
				     secret.password_len, secret.scratch, secret.scratch_size);
}

static void opal_s3_unlock_if_armed(void)
{
	u32 rc;

	if (!secret.valid || !secret.armed)
		return;

	if (secret.armed_cycle != secret.sleep_cycle) {
		printk(BIOS_ERR, "OPAL: unlock rejected (invalid sequence)\n");
		opal_s3_clear_secret();
		return;
	}

	secret.armed = false;
	secret.armed_cycle = 0;

	rc = opal_s3_unlock();
	if (rc)
		printk(BIOS_ERR, "OPAL: unlock failed (rc=%u)\n", rc);

	/* Always clear secrets after an attempt. */
	opal_s3_clear_secret();
}

int opal_s3_smi_apmc(u8 apmc)
{
	int node;
	u64 rax;
	u64 rbx;
	u8 subcmd;
	u32 ret;

	/*
	 * Reduce attack surface: only handle the OPAL service command and the
	 * resume unlock trigger. Ignore everything else.
	 */
	switch (apmc) {
	case APM_CNT_OPAL_S3_UNLOCK:
		opal_s3_unlock_if_armed();
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
		break;
	case OPAL_SMM_SUBCMD_CLEAR_SECRET:
		ret = opal_s3_clear_secret();
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
