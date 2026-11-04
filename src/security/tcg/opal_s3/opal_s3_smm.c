/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <commonlib/bsd/helpers.h>
#include <console/console.h>
#include <cpu/x86/save_state.h>
#include <cpu/x86/smm.h>
#include <delay.h>
#include <efi_variable_service.h>
#include <device/pci_def.h>
#include <device/pci_ops.h>
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

#define OPAL_S3_UNLOCK_RETRY_DELAY_MS   50
#define OPAL_S3_UNLOCK_RETRIES          10

#define OPAL_S3_STATE_SIGNATURE 0x33534c4f /* "OLS3" */
#define OPAL_S3_STATE_VERSION   0x0002

#define OPAL_S3_ARMED_NONE      0
#define OPAL_S3_ARMED_S3        1

#define OPAL_S3_MAX_SECRETS 8

struct __packed opal_s3_secret {
	u8 valid;
	u8 reserved0;
	u16 reserved1;

	u8 bus;
	u8 dev;
	u8 func;
	u8 reserved2;

	u16 base_comid;
	u8 password_len;
	u8 reserved3;
	u8 password[OPAL_S3_MAX_PASSWORD_LEN];

	u32 nvme_bar0_low;
	u32 nvme_bar0_high;
	u32 nvme_id;
	size_t nvme_mmio_size;

	u32 unlocked_cycle;
};

struct __packed opal_s3_state {
	u32 signature;
	u16 version;
	u16 size;

	u8 armed_state;
	u8 reserved0;
	u16 reserved1;

	u32 sleep_cycle;
	u32 armed_cycle;

	struct opal_s3_secret secret[OPAL_S3_MAX_SECRETS];
};

_Static_assert(sizeof(struct opal_s3_state) <= CONFIG_SMM_OPAL_S3_STATE_SMRAM_SIZE,
	       "OPAL state exceeds its retained SMRAM reservation");

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

	if (smm_points_to_smram((void *)(uintptr_t)aligned, OPAL_S3_SCRATCH_MIN_BYTES))
		return false;

	if (aligned + OPAL_S3_SCRATCH_MIN_BYTES < aligned)
		return false;
	if (aligned + OPAL_S3_SCRATCH_MIN_BYTES > base + size)
		return false;

	*aligned_base_out = aligned;
	*aligned_size_out = aligned_size;
	return true;
}

static u32 opal_s3_clear_secret(void)
{
	struct opal_s3_state *st = opal_s3_get_state();

	opal_explicit_bzero(st, sizeof(*st));

	return 0;
}

static struct opal_s3_secret *opal_s3_find_secret(struct opal_s3_state *st,
						  const struct opal_s3_smm_ctx *ctx)
{
	for (size_t i = 0; i < ARRAY_SIZE(st->secret); i++) {
		struct opal_s3_secret *s = &st->secret[i];

		if (!s->valid)
			continue;

		if (s->bus == ctx->bus && s->dev == ctx->dev && s->func == ctx->func &&
		    s->base_comid == ctx->base_comid)
			return s;
	}

	return NULL;
}

static struct opal_s3_secret *opal_s3_alloc_secret(struct opal_s3_state *st)
{
	for (size_t i = 0; i < ARRAY_SIZE(st->secret); i++) {
		struct opal_s3_secret *s = &st->secret[i];

		if (!s->valid)
			return s;
	}

	return NULL;
}

static const volatile struct resource *opal_s3_trusted_bar(pci_devfn_t dev)
{
	const volatile struct smm_pci_resource_info *slots;
	size_t count;

	smm_pci_get_stored_resources(&slots, &count);
	for (size_t i = 0; i < count; i++) {
		const volatile struct smm_pci_resource_info *slot = &slots[i];

		if (!slot->vendor_id || slot->pci_addr != dev ||
		    (((u32)slot->class_device << 8) | slot->class_prog) != OPAL_NVME_CLASS ||
		    pci_read_config32(dev, PCI_VENDOR_ID) !=
			((u32)slot->device_id << 16 | slot->vendor_id) ||
		    (pci_read_config32(dev, PCI_CLASS_REVISION) >> 8) != OPAL_NVME_CLASS)
			continue;
		for (size_t j = 0; j < ARRAY_SIZE(slot->resources); j++) {
			const volatile struct resource *res = &slot->resources[j];

			if (res->index == PCI_BASE_ADDRESS_0 &&
			    (res->flags & (IORESOURCE_MEM | IORESOURCE_ASSIGNED)) ==
				(IORESOURCE_MEM | IORESOURCE_ASSIGNED))
				return res;
		}
	}
	return NULL;
}

static u32 opal_s3_set_secret_snapshot(const struct opal_s3_smm_ctx *ctx)
{
	struct opal_s3_state *st = opal_s3_get_state();
	struct opal_s3_secret *s;
	uintptr_t scratch_base = 0;
	size_t scratch_size = 0;
	uintptr_t aligned_base = 0;
	size_t aligned_size = 0;
	pci_devfn_t nvme_dev;
	const volatile struct resource *bar;
	u32 bar_low, bar_high;
	u64 base;

	if (ctx->signature != OPAL_S3_SMM_CTX_SIGNATURE)
		return 3;

	if (ctx->version != OPAL_S3_SMM_CTX_VERSION)
		return 4;

	if (ctx->size < sizeof(*ctx))
		return 5;

	if (ctx->password_len == 0 || ctx->password_len > OPAL_S3_MAX_PASSWORD_LEN)
		return 6;

	smm_get_opal_s3_scratch_buffer(&scratch_base, &scratch_size);
	if (!opal_s3_validate_scratch(scratch_base, scratch_size, &aligned_base,
				      &aligned_size)) {
		printk(BIOS_ERR, "OPAL: invalid scratch (base=0x%lx size=0x%zx)\n",
		       (unsigned long)scratch_base, scratch_size);
		return 7;
	}

	if (ctx->dev > 31 || ctx->func > 7)
		return 9;
	nvme_dev = PCI_DEV(ctx->bus, ctx->dev, ctx->func);
	bar = opal_s3_trusted_bar(nvme_dev);
	if (!bar || !bar->base || bar->size < 0x2000 ||
	    bar->base > UINTPTR_MAX || bar->size - 1 > UINTPTR_MAX - bar->base)
		return 9;
	bar_low = pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0);
	bar_high = (bar_low & PCI_BASE_ADDRESS_MEM_LIMIT_MASK) ==
		PCI_BASE_ADDRESS_MEM_LIMIT_64 ? pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0 + 4) : 0;
	base = ((u64)bar_high << 32) | (bar_low & ~PCI_BASE_ADDRESS_MEM_ATTR_MASK);
	/* Enrollment uses the trusted cold-boot allocation, never arbitrary live MMIO. */
	if ((bar_low & PCI_BASE_ADDRESS_SPACE) || base != bar->base ||
	    smm_points_to_smram((void *)(uintptr_t)base, bar->size))
		return 9;

	/* Ineligible devices were classified without modifying retained state or hardware.
	 * MM retains handoff closure across S3 and CLEAR_SECRET. */
	if (CONFIG(EFI_VARIABLE_SERVICE) && !efi_variable_service_boot_access_allowed())
		return 10;

	if (st->signature != OPAL_S3_STATE_SIGNATURE || st->version != OPAL_S3_STATE_VERSION ||
	    st->size != sizeof(*st)) {
		opal_s3_clear_secret();
		st->signature = OPAL_S3_STATE_SIGNATURE;
		st->version = OPAL_S3_STATE_VERSION;
		st->size = sizeof(*st);
		st->armed_state = OPAL_S3_ARMED_NONE;
	}

	s = opal_s3_find_secret(st, ctx);
	if (!s)
		s = opal_s3_alloc_secret(st);
	if (!s)
		return 8;

	opal_explicit_bzero(s, sizeof(*s));
	s->valid = 1;
	s->bus = ctx->bus;
	s->dev = ctx->dev;
	s->func = ctx->func;
	s->base_comid = ctx->base_comid;
	s->password_len = ctx->password_len;
	memcpy(s->password, ctx->password, ctx->password_len);

	st->signature = OPAL_S3_STATE_SIGNATURE;
	st->version = OPAL_S3_STATE_VERSION;
	st->size = sizeof(*st);

	s->nvme_bar0_low = bar_low;
	s->nvme_bar0_high = bar_high;
	s->nvme_id = pci_read_config32(nvme_dev, PCI_VENDOR_ID);
	s->nvme_mmio_size = bar->size;

	return 0;
}

static u32 opal_s3_set_secret(const struct opal_s3_smm_ctx *ctx)
{
	struct opal_s3_smm_ctx snapshot;
	u32 ret;

	if (!ctx)
		return 1;

	if (smm_points_to_smram(ctx, sizeof(*ctx)))
		return 2;

	/* The caller's buffer can change while SMM is handling the request. */
	memcpy(&snapshot, ctx, sizeof(snapshot));
	ret = opal_s3_set_secret_snapshot(&snapshot);
	opal_explicit_bzero(&snapshot, sizeof(snapshot));
	return ret;
}

static void opal_s3_arm_for_s3(void)
{
	struct opal_s3_state *st = opal_s3_get_state();
	bool any_secret = false;

	if (st->signature != OPAL_S3_STATE_SIGNATURE || st->version != OPAL_S3_STATE_VERSION ||
	    st->size != sizeof(*st))
		return;

	if (st->armed_state == OPAL_S3_ARMED_S3)
		return;

	for (size_t i = 0; i < ARRAY_SIZE(st->secret); i++) {
		if (!st->secret[i].valid)
			continue;

		any_secret = true;
		st->secret[i].unlocked_cycle = 0;
	}
	if (!any_secret)
		return;

	st->sleep_cycle++;
	st->armed_cycle = st->sleep_cycle;
	st->armed_state = OPAL_S3_ARMED_S3;
}

static bool opal_s3_restore_nvme_bar0_if_needed(pci_devfn_t nvme_dev, u32 saved_bar0_low,
						u32 saved_bar0_high)
{
	const u32 cur_low = pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0);
	const bool is_saved_mem =
		((saved_bar0_low & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY);
	const bool is_saved_64 =
		((saved_bar0_low & PCI_BASE_ADDRESS_MEM_LIMIT_MASK) == PCI_BASE_ADDRESS_MEM_LIMIT_64);
	const u64 saved_base = (saved_bar0_low & ~PCI_BASE_ADDRESS_MEM_ATTR_MASK) |
		(is_saved_64 ? (u64)saved_bar0_high << 32 : 0);

	if (cur_low == saved_bar0_low &&
	    (!is_saved_64 || pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0 + 4) == saved_bar0_high))
		return true;

	if (!saved_bar0_low || saved_base == 0 || !is_saved_mem)
		return false;

	/* Decode and DMA stay off while restoring the enrollment-owned allocation. */
	pci_and_config16(nvme_dev, PCI_COMMAND, ~(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER));
	if (pci_read_config16(nvme_dev, PCI_COMMAND) & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER))
		return false;
	if (is_saved_64)
		pci_write_config32(nvme_dev, PCI_BASE_ADDRESS_0 + 4, saved_bar0_high);
	pci_write_config32(nvme_dev, PCI_BASE_ADDRESS_0, saved_bar0_low);
	return pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0) == saved_bar0_low &&
		(!is_saved_64 || pci_read_config32(nvme_dev, PCI_BASE_ADDRESS_0 + 4) == saved_bar0_high);
}

static bool opal_s3_unlock_should_keep_armed(u32 rc)
{
	return rc == 1 || rc == 3;
}

static u32 opal_s3_unlock_if_armed(void)
{
	struct opal_s3_state *st = opal_s3_get_state();
	u32 rc = 0;
	uintptr_t scratch_base = 0;
	size_t scratch_size = 0;
	uintptr_t aligned_base = 0;
	size_t aligned_size = 0;
	bool keep_armed = false;

	if (st->signature != OPAL_S3_STATE_SIGNATURE || st->version != OPAL_S3_STATE_VERSION ||
	    st->size != sizeof(*st))
		return 0x10;

	if (st->armed_state == OPAL_S3_ARMED_NONE)
		return 0x11;

	if (st->armed_cycle != st->sleep_cycle) {
		printk(BIOS_ERR, "OPAL: unlock rejected (invalid sequence)\n");
		opal_s3_clear_secret();
		return 0x12;
	}

	smm_get_opal_s3_scratch_buffer(&scratch_base, &scratch_size);
	if (!opal_s3_validate_scratch(scratch_base, scratch_size, &aligned_base,
				      &aligned_size)) {
		printk(BIOS_ERR, "OPAL: scratch invariant failed at unlock\n");
		st->armed_state = OPAL_S3_ARMED_NONE;
		st->armed_cycle = 0;
		return 2;
	}

	for (size_t i = 0; i < ARRAY_SIZE(st->secret); i++) {
		struct opal_s3_secret *s = &st->secret[i];
		pci_devfn_t nvme_dev;
		u32 one_rc;

		if (!s->valid)
			continue;
		if (s->password_len == 0 || s->password_len > OPAL_S3_MAX_PASSWORD_LEN)
			continue;
		if (s->unlocked_cycle == st->sleep_cycle)
			continue;

		nvme_dev = PCI_DEV(s->bus, s->dev, s->func);
		/* Check immutable device identity before power, decode, BAR or MMIO changes. */
		if (pci_read_config32(nvme_dev, PCI_VENDOR_ID) != s->nvme_id ||
		    (pci_read_config32(nvme_dev, PCI_CLASS_REVISION) >> 8) != OPAL_NVME_CLASS) {
			rc = 9;
			continue;
		}
		if (!opal_s3_restore_nvme_bar0_if_needed(nvme_dev, s->nvme_bar0_low,
						      s->nvme_bar0_high)) {
			rc = 9;
			continue;
		}
		const u64 mmio_base = ((u64)s->nvme_bar0_high << 32) |
			(s->nvme_bar0_low & ~PCI_BASE_ADDRESS_MEM_ATTR_MASK);

		one_rc = 1;
		for (int attempt = 0; attempt < OPAL_S3_UNLOCK_RETRIES; attempt++) {
			if (attempt)
				mdelay(OPAL_S3_UNLOCK_RETRY_DELAY_MS);

			one_rc = opal_nvme_opal_unlock(nvme_dev, s->base_comid, s->password,
						       s->password_len,
						       (void *)(uintptr_t)aligned_base,
						       aligned_size, mmio_base, s->nvme_mmio_size);
			if (one_rc == 0)
				break;
			if (one_rc != 1)
				break;
		}

		if (one_rc == 0) {
			s->unlocked_cycle = st->sleep_cycle;
			continue;
		}

		if (opal_s3_unlock_should_keep_armed(one_rc)) {
			keep_armed = true;
			if (rc == 0 || rc == 1)
				rc = one_rc;
			continue;
		}

		if (rc == 0)
			rc = one_rc;

		s->unlocked_cycle = st->sleep_cycle;
	}

	if (!keep_armed) {
		st->armed_state = OPAL_S3_ARMED_NONE;
		st->armed_cycle = 0;
	}

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

	switch (apmc) {
	case APM_CNT_OPAL_S3_UNLOCK:
		unlock_rc = opal_s3_unlock_if_armed();

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
	if (slp_typ == ACPI_S3)
		opal_s3_arm_for_s3();
	else
		opal_s3_clear_secret();
}

void opal_s3_smi_sleep_finalize(u8 slp_typ)
{
	if (slp_typ != ACPI_S3)
		opal_s3_clear_secret();
}
