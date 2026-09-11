/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <device/mmio.h>
#include <fsp/util.h>
#include <intelblocks/systemagent.h>
#include <intelblocks/vtd.h>
#include <lib.h>
#include <soc/iomap.h>
#include <soc/pci_devs.h>
#include <timer.h>

/* FSP 2.x VT-d HOB from edk2-platforms */
static const uint8_t vtd_pmr_info_data_hob_guid[16] = {
	0x45, 0x16, 0xb6, 0x6f, 0x68, 0xf1, 0xbe, 0x46,
	0x80, 0xec, 0xb5, 0x02, 0x38, 0x5e, 0xe7, 0xe7
};

struct vtd_pmr_info_hob {
	/* EDK2 VTD_PMR_INFO_HOB uses exclusive limits, not raw PMR register limits. */
	uint32_t protected_low_base;
	uint32_t protected_low_limit;
	uint64_t protected_high_base;
	uint64_t protected_high_limit;
} __packed;

static struct vtd_pmr_info_hob *pmr_hob;

static bool is_vtd_enabled(uintptr_t vtd_base)
{
	uint32_t version = vtd_read32(vtd_base, VER_REG);

	if (version == 0 || version == UINT32_MAX) {
		printk(BIOS_WARNING, "No VT-d @ 0x%08lx\n", vtd_base);
		return false;
	}

	printk(BIOS_DEBUG, "VT-d @ 0x%08lx, version %x.%x\n",
	       vtd_base, (version & 0xf0) >> 4, version & 0xf);

	return true;
}

static uint32_t vtd_get_pmr_alignment_lo(uintptr_t vtd_base)
{
	uint32_t value;

	vtd_write32(vtd_base, PLMLIMIT_REG, 0xffffffff);
	value = vtd_read32(vtd_base, PLMLIMIT_REG);
	value = ~value + 1;

	return value;
}

static uint64_t vtd_get_pmr_alignment_hi(uintptr_t vtd_base)
{
	const uint32_t phys_bits = cpu_phys_address_size();
	uint64_t value;

	vtd_write64(vtd_base, PHMLIMIT_REG, 0xffffffffffffffffULL);
	value = vtd_read64(vtd_base, PHMLIMIT_REG);
	value = ~value + 1ULL;
	if (phys_bits < 64)
		value &= (1ULL << phys_bits) - 1ULL;

	/* The host address width can be different than the sizing of the register.
	 * Simply find the least significant bit set and use it as alignment;
	 */
	return value ? 1ULL << __ffs64(value) : 0;
}

static bool vtd_set_pmr_low(uintptr_t vtd_base, uint32_t *alignment)
{
	uint32_t pmr_lo_align;
	uint32_t pmr_lo_limit;
	/*
	 * Typical PMR alignment is 1MB so we should be good but check just in
	 * case.
	 */
	pmr_lo_align = vtd_get_pmr_alignment_lo(vtd_base);
	*alignment = pmr_lo_align;
	pmr_lo_limit = pmr_hob->protected_low_limit;

	if (!pmr_lo_align || !IS_ALIGNED(pmr_lo_limit, pmr_lo_align)) {
		printk(BIOS_ERR, "PMR low limit is not representable: %08x\n", pmr_lo_limit);
		return false;
	}

	printk(BIOS_INFO, "Setting DMA protection [0x0 - 0x%08x]\n", pmr_lo_limit);
	vtd_write32(vtd_base, PLMBASE_REG, 0);
	vtd_write32(vtd_base, PLMLIMIT_REG, pmr_lo_limit - 1);

	return vtd_read32(vtd_base, PLMBASE_REG) == 0 &&
		vtd_read32(vtd_base, PLMLIMIT_REG) ==
			((pmr_lo_limit - 1) & ~(pmr_lo_align - 1));
}

static bool vtd_set_pmr_high(uintptr_t vtd_base, uint64_t *alignment)
{
	uint64_t pmr_hi_align;
	uint64_t pmr_hi_limit;

	/* PMEN enables both ranges; do not reactivate a stale unused high range. */
	if (pmr_hob->protected_high_base == pmr_hob->protected_high_limit) {
		vtd_write64(vtd_base, PHMBASE_REG, UINT64_MAX);
		vtd_write64(vtd_base, PHMLIMIT_REG, 0);
		return vtd_read64(vtd_base, PHMBASE_REG) >
			vtd_read64(vtd_base, PHMLIMIT_REG);
	}

	/*
	 * Typical PMR alignment is 1MB so we should be good with above 4G
	 * memory but check just in case.
	 */
	pmr_hi_align = vtd_get_pmr_alignment_hi(vtd_base);
	*alignment = pmr_hi_align;
	pmr_hi_limit = pmr_hob->protected_high_limit;

	if (!pmr_hi_align || !IS_ALIGNED(pmr_hi_limit, pmr_hi_align)) {
		printk(BIOS_ERR, "PMR high limit is not representable: %016llx\n",
		       pmr_hi_limit);
		return false;
	}

	printk(BIOS_INFO, "Setting DMA protection [0x100000000 - 0x%016llx]\n", pmr_hi_limit);
	vtd_write64(vtd_base, PHMBASE_REG, 4ULL * GiB);
	vtd_write64(vtd_base, PHMLIMIT_REG, pmr_hi_limit - 1ULL);

	return vtd_read64(vtd_base, PHMBASE_REG) == 4ULL * GiB &&
		vtd_read64(vtd_base, PHMLIMIT_REG) ==
			((pmr_hi_limit - 1) & ~(pmr_hi_align - 1));
}

static bool set_pmr_protection(uintptr_t vtd_base, bool enable)
{
	uint32_t pmen;
	const uint32_t expected = enable ? PMEN_EPM | PMEN_PRS : 0;

	/* Complete any outstanding command before changing EPM (VT-d 11.4.8.1). */
	if (!wait_us(USECS_PER_SEC,
		(pmen = vtd_read32(vtd_base, PMEN_REG)) != UINT32_MAX &&
		!!(pmen & PMEN_EPM) == !!(pmen & PMEN_PRS)))
		return false;

	vtd_write32(vtd_base, PMEN_REG, enable ? pmen | PMEN_EPM : pmen & ~PMEN_EPM);
	return wait_us(USECS_PER_SEC,
		(pmen = vtd_read32(vtd_base, PMEN_REG)) != UINT32_MAX &&
		(pmen & (PMEN_EPM | PMEN_PRS)) == expected);
}

static bool pmr_ranges_match(uintptr_t vtd_base, bool high_required,
			     uint32_t low_alignment, uint64_t high_alignment)
{
	if (vtd_read32(vtd_base, PLMBASE_REG) != 0 ||
	    vtd_read32(vtd_base, PLMLIMIT_REG) !=
		((pmr_hob->protected_low_limit - 1) & ~(low_alignment - 1)))
		return false;

	if (!high_required)
		return !(vtd_read32(vtd_base, CAP_REG) & CAP_PMR_HI) ||
			vtd_read64(vtd_base, PHMBASE_REG) >
			vtd_read64(vtd_base, PHMLIMIT_REG);

	return vtd_read64(vtd_base, PHMBASE_REG) == pmr_hob->protected_high_base &&
		vtd_read64(vtd_base, PHMLIMIT_REG) ==
			((pmr_hob->protected_high_limit - 1) & ~(high_alignment - 1));
}

static const void *locate_pmr_info_hob(void)
{
	size_t size;
	const void *hob;

	if (pmr_hob)
		return (void *)pmr_hob;

	hob = fsp_find_extension_hob_by_guid(vtd_pmr_info_data_hob_guid, &size);

	if (hob && size >= sizeof(*pmr_hob)) {
		pmr_hob = (struct vtd_pmr_info_hob *)hob;
		printk(BIOS_SPEW, "PMR info HOB:\n"
				  "  protected_low_base: %08x\n"
				  "  protected_low_limit: %08x\n"
				  "  protected_high_base: %016llx\n"
				  "  protected_high_limit: %016llx\n",
				  pmr_hob->protected_low_base, pmr_hob->protected_low_limit,
				  pmr_hob->protected_high_base, pmr_hob->protected_high_limit);
	} else if (hob) {
		printk(BIOS_ERR, "FSP PMR info HOB is too small\n");
		hob = NULL;
	}

	return hob;
}

static bool vtd_engine_enable_dma_protection(uintptr_t vtd_base)
{
	uint32_t cap;
	uint32_t low_alignment;
	uint64_t high_alignment = 0;
	bool high_required;

	if (!is_vtd_enabled(vtd_base)) {
		printk(BIOS_ERR, "Not enabling DMA protection, VT-d not found\n");
		return false;
	}

	cap = vtd_read32(vtd_base, CAP_REG);
	if (!(cap & CAP_PMR_LO)) {
		printk(BIOS_ERR, "Not enabling DMA protection, PMR registers not supported\n");
		return false;
	}

	if (!locate_pmr_info_hob()) {
		printk(BIOS_ERR, "VT-d PMR HOB not found, not enabling DMA protection\n");
		return false;
	}

	high_required = pmr_hob->protected_high_limit > 4ULL * GiB;
	if (pmr_hob->protected_low_base != 0 || pmr_hob->protected_low_limit == 0 ||
	    (high_required && pmr_hob->protected_high_base != 4ULL * GiB) ||
	    (!high_required &&
	     (pmr_hob->protected_high_base != pmr_hob->protected_high_limit ||
	      (pmr_hob->protected_high_base != 0 &&
	       pmr_hob->protected_high_base != 4ULL * GiB)))) {
		printk(BIOS_ERR, "FSP PMR info HOB contains invalid ranges\n");
		return false;
	}

	if (high_required && !(cap & CAP_PMR_HI)) {
		printk(BIOS_ERR, "VT-d high PMR is required but unsupported\n");
		return false;
	}

	/* If protection is enabled, disable it first */
	if (!set_pmr_protection(vtd_base, false)) {
		printk(BIOS_ERR, "Not setting DMA protection\n");
		return false;
	}

	if (!vtd_set_pmr_low(vtd_base, &low_alignment))
		return false;

	if ((cap & CAP_PMR_HI) && !vtd_set_pmr_high(vtd_base, &high_alignment))
		return false;

	if (set_pmr_protection(vtd_base, true) &&
	    pmr_ranges_match(vtd_base, high_required, low_alignment, high_alignment)) {
		printk(BIOS_INFO, "Successfully enabled VT-d PMR DMA protection\n");
		return true;
	}

	printk(BIOS_ERR, "Enabling VT-d PMR DMA protection failed\n");
	return false;
}

static const struct hob_resource *find_resource_hob_by_addr(const uint64_t addr)
{
	const struct hob_header *hob_iterator;
	const struct hob_resource *res;

	if (fsp_hob_iterator_init(&hob_iterator) != CB_SUCCESS) {
		printk(BIOS_ERR, "Failed to find HOB list\n");
		return NULL;
	}

	while (fsp_hob_iterator_get_next_resource(&hob_iterator, &res) == CB_SUCCESS) {
		if ((res->type == EFI_RESOURCE_MEMORY_RESERVED) && (res->addr == addr))
			return res;
	}

	return NULL;
}

void *vtd_get_dma_buffer(size_t *size)
{
	const struct hob_resource *res;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION))
		goto no_dma_buffer;

	if (!locate_pmr_info_hob()) {
		printk(BIOS_ERR, "FSP PMR info HOB not found\n");
		goto no_dma_buffer;
	}

	/* PMR low limit will be the DMA buffer base reserved by FSP */
	res = find_resource_hob_by_addr((uint64_t)pmr_hob->protected_low_limit);
	if (!res) {
		printk(BIOS_ERR, "FSP PMR resource HOB not found\n");
		goto no_dma_buffer;
	}

	if (size)
		*size = res->length;

	return (void *)(uintptr_t)res->addr;

no_dma_buffer:
	if (size)
		*size = 0;
	return NULL;
}

bool vtd_enable_dma_protection(void)
{
	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION))
		return false;

	/*
	 * FIXME: GFX VT-d will fail to set PMR (tested on ADL-S).
	 * Should we program PMRs on all VT-d engines?
	 * vtd_engine_enable_dma_protection(GFXVT_BASE_ADDRESS);
	 * vtd_engine_enable_dma_protection(IPUVT_BASE_ADDRESS);
	 */
	return vtd_engine_enable_dma_protection(VTVC0_BASE_ADDRESS);
}

static void vtd_disable_pmr_on_resume(void *unused)
{
	/* At minimum PMR Low must be supported */
	if (!(vtd_read32(VTVC0_BASE_ADDRESS, CAP_REG) & CAP_PMR_LO))
		return;

	if (set_pmr_protection(VTVC0_BASE_ADDRESS, false)) {
		vtd_write32(VTVC0_BASE_ADDRESS, PLMBASE_REG, 0);
		vtd_write32(VTVC0_BASE_ADDRESS, PLMLIMIT_REG, 0);
		if (vtd_read32(VTVC0_BASE_ADDRESS, CAP_REG) & CAP_PMR_HI) {
			vtd_write64(VTVC0_BASE_ADDRESS, PHMBASE_REG, 0);
			vtd_write64(VTVC0_BASE_ADDRESS, PHMLIMIT_REG, 0);
		}
	}
}

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME, BS_ON_ENTRY, vtd_disable_pmr_on_resume, NULL);
