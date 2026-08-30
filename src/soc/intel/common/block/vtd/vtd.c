/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootstate.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <device/mmio.h>
#include <fsp/util.h>
#include <intelblocks/vtd.h>
#include <lib.h>
#include <timer.h>

#define PMR_STATE_TIMEOUT_US 1000

/* FSP 2.x VT-d HOB from edk2-platforms */
static const uint8_t vtd_pmr_info_data_hob_guid[16] = {
	0x45, 0x16, 0xb6, 0x6f, 0x68, 0xf1, 0xbe, 0x46,
	0x80, 0xec, 0xb5, 0x02, 0x38, 0x5e, 0xe7, 0xe7
};

struct vtd_pmr_info_hob {
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
		printk(BIOS_ERR, "No VT-d @ 0x%08lx\n", vtd_base);
		return false;
	}

	printk(BIOS_DEBUG, "VT-d @ 0x%08lx, version %x.%x\n",
	       vtd_base, (version & 0xf0) >> 4, version & 0xf);

	return true;
}

static bool vtd_get_pmr_alignments_lo(uintptr_t vtd_base, uint32_t *base_alignment,
				      uint32_t *limit_alignment)
{
	uint32_t base_value;
	uint32_t limit_value;

	vtd_write32(vtd_base, PLMBASE_REG, UINT32_MAX);
	vtd_write32(vtd_base, PLMLIMIT_REG, UINT32_MAX);
	base_value = vtd_read32(vtd_base, PLMBASE_REG);
	limit_value = vtd_read32(vtd_base, PLMLIMIT_REG);
	*base_alignment = ~base_value + 1;
	*limit_alignment = ~limit_value + 1;

	return *base_alignment && IS_POWER_OF_2(*base_alignment) &&
		*limit_alignment && IS_POWER_OF_2(*limit_alignment);
}

static bool vtd_get_pmr_alignments_hi(uintptr_t vtd_base, uint64_t *base_alignment,
				      uint64_t *limit_alignment)
{
	unsigned int address_bits;
	uint64_t address_mask;
	uint64_t base_value;
	uint64_t limit_value;

	address_bits = cpu_phys_address_size();
	if (!address_bits || address_bits >= 64)
		return false;

	address_mask = (1ULL << address_bits) - 1;
	vtd_write64(vtd_base, PHMBASE_REG, UINT64_MAX);
	vtd_write64(vtd_base, PHMLIMIT_REG, UINT64_MAX);
	base_value = vtd_read64(vtd_base, PHMBASE_REG) & address_mask;
	limit_value = vtd_read64(vtd_base, PHMLIMIT_REG) & address_mask;
	*base_alignment = ((~base_value) & address_mask) + 1;
	*limit_alignment = ((~limit_value) & address_mask) + 1;

	return *base_alignment < (1ULL << address_bits) &&
		IS_POWER_OF_2(*base_alignment) &&
		*limit_alignment < (1ULL << address_bits) &&
		IS_POWER_OF_2(*limit_alignment);
}

static bool vtd_set_pmr_low(uintptr_t vtd_base, uint32_t *expected_base,
			    uint32_t *expected_limit)
{
	uint32_t base_alignment;
	uint32_t limit_alignment;
	uint32_t pmr_lo_base = pmr_hob->protected_low_base;
	uint32_t pmr_lo_limit = pmr_hob->protected_low_limit;
	uint32_t encoded_limit;

	if (!vtd_get_pmr_alignments_lo(vtd_base, &base_alignment, &limit_alignment)) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx has invalid low PMR alignment\n",
		       vtd_base);
		return false;
	}
	if (pmr_lo_base >= pmr_lo_limit || !IS_ALIGNED(pmr_lo_base, base_alignment) ||
	    !IS_ALIGNED(pmr_lo_limit, limit_alignment)) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx rejects low PMR range %08x-%08x\n",
		       vtd_base, pmr_lo_base, pmr_lo_limit);
		return false;
	}

	encoded_limit = pmr_lo_limit - limit_alignment;
	*expected_base = pmr_lo_base;
	*expected_limit = encoded_limit;

	printk(BIOS_INFO, "Setting DMA protection [0x%08x, 0x%08x)\n",
	       pmr_lo_base, pmr_lo_limit);
	vtd_write32(vtd_base, PLMBASE_REG, pmr_lo_base);
	vtd_write32(vtd_base, PLMLIMIT_REG, encoded_limit);

	if (vtd_read32(vtd_base, PLMBASE_REG) != pmr_lo_base ||
	    vtd_read32(vtd_base, PLMLIMIT_REG) != encoded_limit) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx low PMR verification failed\n",
		       vtd_base);
		return false;
	}

	return true;
}

static bool vtd_set_pmr_high(uintptr_t vtd_base, uint64_t *expected_base,
			     uint64_t *expected_limit)
{
	uint64_t base_alignment;
	uint64_t limit_alignment;
	uint64_t pmr_hi_base = pmr_hob->protected_high_base;
	uint64_t pmr_hi_limit = pmr_hob->protected_high_limit;
	uint64_t encoded_limit;
	uint64_t disabled_base;

	if (!vtd_get_pmr_alignments_hi(vtd_base, &base_alignment, &limit_alignment)) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx has invalid high PMR alignment\n",
		       vtd_base);
		return false;
	}
	if (!pmr_hi_base && !pmr_hi_limit) {
		/* A zero limit decodes through limit_alignment - 1. */
		disabled_base = MAX(base_alignment, limit_alignment);
		*expected_base = disabled_base;
		*expected_limit = 0;
		vtd_write64(vtd_base, PHMBASE_REG, disabled_base);
		vtd_write64(vtd_base, PHMLIMIT_REG, 0);
		if (vtd_read64(vtd_base, PHMBASE_REG) != disabled_base ||
		    vtd_read64(vtd_base, PHMLIMIT_REG) != 0) {
			printk(BIOS_ERR, "VT-d @ 0x%08lx cannot disable its high PMR\n",
			       vtd_base);
			return false;
		}
		return true;
	}
	if (pmr_hi_base >= pmr_hi_limit || !IS_ALIGNED(pmr_hi_base, base_alignment) ||
	    !IS_ALIGNED(pmr_hi_limit, limit_alignment)) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx rejects high PMR range "
		       "%016llx-%016llx\n", vtd_base, pmr_hi_base, pmr_hi_limit);
		return false;
	}

	encoded_limit = pmr_hi_limit - limit_alignment;
	*expected_base = pmr_hi_base;
	*expected_limit = encoded_limit;

	printk(BIOS_INFO, "Setting DMA protection [0x%016llx, 0x%016llx)\n",
	       pmr_hi_base, pmr_hi_limit);
	vtd_write64(vtd_base, PHMBASE_REG, pmr_hi_base);
	vtd_write64(vtd_base, PHMLIMIT_REG, encoded_limit);

	if (vtd_read64(vtd_base, PHMBASE_REG) != pmr_hi_base ||
	    vtd_read64(vtd_base, PHMLIMIT_REG) != encoded_limit) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx high PMR verification failed\n",
		       vtd_base);
		return false;
	}

	return true;
}

static bool set_pmr_protection(uintptr_t vtd_base, bool enable)
{
	uint32_t pmen;

	if (!wait_us(PMR_STATE_TIMEOUT_US, ({
		pmen = vtd_read32(vtd_base, PMEN_REG);
		!!(pmen & PMEN_EPM) == !!(pmen & PMEN_PRS);
	}))) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx PMR state did not settle\n", vtd_base);
		return false;
	}
	if (!!(pmen & PMEN_PRS) == enable)
		return true;

	pmen = enable ? pmen | PMEN_EPM : pmen & ~PMEN_EPM;
	vtd_write32(vtd_base, PMEN_REG, pmen);
	if (wait_us(PMR_STATE_TIMEOUT_US, ({
		pmen = vtd_read32(vtd_base, PMEN_REG);
		!!(pmen & PMEN_EPM) == enable && !!(pmen & PMEN_PRS) == enable;
	})))
		return true;

	printk(BIOS_ERR, "Timed out %s PMRs on VT-d @ 0x%08lx\n",
	       enable ? "enabling" : "disabling", vtd_base);
	return false;
}

static bool disable_pmr_protection(uintptr_t vtd_base)
{
	return set_pmr_protection(vtd_base, false);
}

static bool enable_pmr_protection(uintptr_t vtd_base)
{
	return set_pmr_protection(vtd_base, true);
}

static const void *locate_pmr_info_hob(void)
{
	size_t size;
	const void *hob;

	if (pmr_hob)
		return (void *)pmr_hob;

	hob = fsp_find_extension_hob_by_guid(vtd_pmr_info_data_hob_guid, &size);
	if (hob && size < sizeof(*pmr_hob)) {
		printk(BIOS_ERR, "VT-d PMR HOB is truncated\n");
		return NULL;
	}

	if (hob) {
		pmr_hob = (struct vtd_pmr_info_hob *)hob;
		printk(BIOS_SPEW, "PMR info HOB:\n"
				  "  protected_low_base: %08x\n"
				  "  protected_low_limit: %08x\n"
				  "  protected_high_base: %016llx\n"
				  "  protected_high_limit: %016llx\n",
				  pmr_hob->protected_low_base, pmr_hob->protected_low_limit,
				  pmr_hob->protected_high_base, pmr_hob->protected_high_limit);
	}

	return hob;
}

static bool vtd_engine_enable_dma_protection(uintptr_t vtd_base)
{
	uint32_t expected_low_base;
	uint32_t expected_low_limit;
	uint64_t expected_high_base = 0;
	uint64_t expected_high_limit = 0;
	bool has_high_pmr;

	/* At minimum PMR Low must be supported, coreboot executes in 32bit space (for now) */
	if (!(vtd_read32(vtd_base, CAP_REG) & CAP_PMR_LO)) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx has no low PMR\n", vtd_base);
		return false;
	}

	if (!locate_pmr_info_hob()) {
		printk(BIOS_ERR, "VT-d PMR HOB not found\n");
		return false;
	}

	/* If protection is enabled, disable it first */
	if (!disable_pmr_protection(vtd_base)) {
		printk(BIOS_ERR, "Cannot reprogram VT-d @ 0x%08lx\n", vtd_base);
		return false;
	}

	if (!vtd_set_pmr_low(vtd_base, &expected_low_base, &expected_low_limit))
		return false;

	has_high_pmr = vtd_read32(vtd_base, CAP_REG) & CAP_PMR_HI;
	if (has_high_pmr) {
		if (!vtd_set_pmr_high(vtd_base, &expected_high_base, &expected_high_limit))
			return false;
	} else if (pmr_hob->protected_high_base || pmr_hob->protected_high_limit) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx cannot cover the high PMR range\n",
		       vtd_base);
		return false;
	}

	if (!enable_pmr_protection(vtd_base)) {
		printk(BIOS_ERR, "Cannot enable PMRs on VT-d @ 0x%08lx\n", vtd_base);
		return false;
	}

	if (vtd_read32(vtd_base, PLMBASE_REG) != expected_low_base ||
	    vtd_read32(vtd_base, PLMLIMIT_REG) != expected_low_limit ||
	    (has_high_pmr &&
	     (vtd_read64(vtd_base, PHMBASE_REG) != expected_high_base ||
	      vtd_read64(vtd_base, PHMLIMIT_REG) != expected_high_limit))) {
		printk(BIOS_ERR, "VT-d @ 0x%08lx PMR verification failed\n", vtd_base);
		return false;
	}

	printk(BIOS_INFO, "Enabled PMR protection on VT-d @ 0x%08lx\n", vtd_base);
	return true;
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
	const uintptr_t *vtd_bases;
	size_t count;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION))
		return false;
	if (!soc_get_vtd_bases(&vtd_bases, &count)) {
		printk(BIOS_ERR, "Cannot identify the active VT-d engines\n");
		return false;
	}
	if (!vtd_bases || !count) {
		printk(BIOS_ERR, "The active VT-d engine list is empty\n");
		return false;
	}

	for (size_t i = 0; i < count; i++) {
		if (!is_vtd_enabled(vtd_bases[i]) ||
		    !vtd_engine_enable_dma_protection(vtd_bases[i]))
			return false;
	}

	return true;
}

static bool vtd_engine_disable_dma_protection(uintptr_t vtd_base)
{
	bool has_high_pmr;

	if (!is_vtd_enabled(vtd_base) ||
	    !(vtd_read32(vtd_base, CAP_REG) & CAP_PMR_LO))
		return false;
	if (!disable_pmr_protection(vtd_base))
		return false;

	has_high_pmr = vtd_read32(vtd_base, CAP_REG) & CAP_PMR_HI;
	vtd_write32(vtd_base, PLMBASE_REG, 0);
	vtd_write32(vtd_base, PLMLIMIT_REG, 0);
	if (has_high_pmr) {
		vtd_write64(vtd_base, PHMBASE_REG, 0);
		vtd_write64(vtd_base, PHMLIMIT_REG, 0);
	}

	if (vtd_read32(vtd_base, PLMBASE_REG) ||
	    vtd_read32(vtd_base, PLMLIMIT_REG) ||
	    (has_high_pmr &&
	     (vtd_read64(vtd_base, PHMBASE_REG) ||
	      vtd_read64(vtd_base, PHMLIMIT_REG))))
		return false;

	return true;
}

static void vtd_disable_pmr_on_resume(void *unused)
{
	const uintptr_t *vtd_bases;
	size_t count;

	if (!CONFIG(ENABLE_EARLY_DMA_PROTECTION))
		return;
	if (!soc_get_vtd_bases(&vtd_bases, &count))
		die("Cannot identify the active VT-d engines on resume\n");
	if (!vtd_bases || !count)
		die("The active VT-d engine list is empty on resume\n");

	for (size_t i = 0; i < count; i++) {
		if (!vtd_engine_disable_dma_protection(vtd_bases[i]))
			die("Cannot disable VT-d PMRs @ 0x%08lx on resume\n",
			    vtd_bases[i]);
	}
}

BOOT_STATE_INIT_ENTRY(BS_OS_RESUME, BS_ON_ENTRY, vtd_disable_pmr_on_resume, NULL);
