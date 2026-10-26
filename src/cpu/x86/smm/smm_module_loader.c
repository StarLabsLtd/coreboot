/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <acpi/acpi_gnvs.h>
#include <cbmem.h>
#include <commonlib/authvar_boot_authority.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <commonlib/bsd/cb_err.h>
#include <console/console.h>
#include <console/payload_spi_console.h>
#include <cpu/cpu.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/gdt.h>
#include <device/device.h>
#include <device/mmio.h>
#include <rmodule.h>
#include <smmstore.h>
#include <stdio.h>
#include <string.h>
#include <types.h>

#define SMM_CODE_SEGMENT_SIZE 0x10000

/*
 * Components that make up the SMRAM:
 * 1. Save state - the total save state memory used
 * 2. Stack - stacks for the CPUs in the SMM handler
 * 3. Stub - SMM stub code for calling into handler
 * 4. Handler - C-based SMM handler.
 *
 * The components are assumed to consist of one consecutive region.
 */

/*
 * The stub is the entry point that sets up protected mode and stacks for each
 * CPU. It then calls into the SMM handler module. It is encoded as an rmodule.
 */
extern unsigned char _binary_smmstub_start[];

/* Per CPU minimum stack size. */
#define SMM_MINIMUM_STACK_SIZE 32

struct cpu_smm_info {
	uint8_t active;
	uintptr_t smbase;
	struct region ss;
	struct region stub_code;
};
struct cpu_smm_info cpus[CONFIG_MAX_CPUS] = { 0 };

/*
 * Carve out a region from the top of the given SMRAM free region.
 * The SMRAM free region shrinks and the newly carved region is returned
 * through the `out` parameter.
 *
 * @arg smram_free: Pointer to the free SMRAM region to carve from.
 * @arg size:       The size of the region to carve out.
 * @arg alignment:  The alignment requirement for the carved region.
 * @arg name:       Name of the region for logging purposes.
 * @arg out:        Pointer to the region structure to store the carved region.
 * Returns CB_SUCCESS on success, CB_ERR if the requested size cannot be accommodated.
 */
static enum cb_err smm_smram_carve_top(struct region *smram_free,
				       const size_t size,
				       const size_t alignment,
				       const char *name,
				       struct region *out)
{
	const size_t smram_region_end = region_last(smram_free) + 1 - size;
	const size_t new_region_end = ALIGN_DOWN(smram_region_end, alignment);
	const size_t size_with_padding = region_last(smram_free) + 1 - new_region_end;

	if (size_with_padding == 0)
		return CB_SUCCESS;

	if (size_with_padding > region_sz(smram_free)) {
		printk(BIOS_ERR, "SMM region %s, size 0x%zx exceeds free SMRAM size 0x%zx\n",
		       name, size, region_sz(smram_free));
		return CB_ERR;
	}

	smram_free->size -= size_with_padding;
	*out = region_create(new_region_end, size_with_padding);
	printk(BIOS_INFO, "SMM SUBREGION: %-20s 0x%zx 0x%zx\n", name, region_offset(out), region_last(out));

	return CB_SUCCESS;
}


/*
 * This method creates a map of all the CPU entry points, save state locations
 * and the beginning and end of code segments for each CPU. This map is used
 * during relocation to properly align as many CPUs that can fit into the SMRAM
 * region. For more information on how SMRAM works, refer to the latest Intel
 * developer's manuals (volume 3, chapter 34). SMRAM is divided up into the
 * following regions:
 * +-----------------+ Top of SMRAM
 * |      MSEG       |
 * +-----------------+
 * |    common       |
 * |  smi handler    | 64K
 * |                 |
 * +-----------------+
 * | CPU 0 code  seg |
 * +-----------------+
 * | CPU 1 code seg  |
 * +-----------------+
 * | CPU x code seg  |
 * +-----------------+
 * |                 |
 * |                 |
 * +-----------------+
 * |    stacks       |
 * +-----------------+ <- START of SMRAM
 *
 * The code below checks when a code segment is full and begins placing the remainder
 * CPUs in the lower segments. The entry point for each CPU is smbase + 0x8000
 * and save state is smbase + 0x8000 + (0x8000 - state save size). Save state
 * area grows downward into the CPUs entry point.  Therefore staggering too many
 * CPUs in one 32K block will corrupt CPU0's entry code as the save states move
 * downward.
 * input : smbase of first CPU (all other CPUs
 *         will go below this address)
 * input : num_cpus in the system. The map will
 *         be created from 0 to num_cpus.
 */
static int smm_create_map(struct region *smram_free, const unsigned int num_cpus,
			  const size_t cpu_save_state_size)
{
	struct rmodule smm_stub;
	char segment_name[32];

	if (ARRAY_SIZE(cpus) < num_cpus) {
		printk(BIOS_ERR, "%s: increase MAX_CPUS in Kconfig\n", __func__);
		return 0;
	}

	if (rmodule_parse(&_binary_smmstub_start, &smm_stub)) {
		printk(BIOS_ERR, "%s: unable to get SMM module size\n", __func__);
		return 0;
	}

	/*
	 * How many CPUs can fit into one 64K segment?
	 * Make sure that the first stub does not overlap with the last save state of a segment.
	 */
	const size_t stub_size = rmodule_memory_size(&smm_stub);
	const size_t needed_ss_size = MAX(cpu_save_state_size, stub_size);
	const size_t cpus_per_segment =
		(SMM_CODE_SEGMENT_SIZE - SMM_ENTRY_OFFSET - stub_size) / needed_ss_size;

	if (cpus_per_segment == 0) {
		printk(BIOS_ERR, "%s: CPUs won't fit in segment. Broken stub or save state size\n",
		       __func__);
		return 0;
	}
	/* ASEG shares one 64K segment with the handler; keep its remaining space. */
	if (!CONFIG(SMM_ASEG)) {
		struct region padding;

		if (smm_smram_carve_top(smram_free, 0, SMM_CODE_SEGMENT_SIZE,
				       "PADDING", &padding) != CB_SUCCESS)
			return 0;
	}

	for (size_t segment = 0; segment < DIV_ROUND_UP(num_cpus, cpus_per_segment); segment++) {
		for (size_t i = 0; i < cpus_per_segment; i++) {
			const size_t cpu_index = segment * cpus_per_segment + i;
			struct cpu_smm_info *cpu_info = &cpus[cpu_index];
			if (cpu_index >= num_cpus)
				break;

			snprintf(segment_name, sizeof(segment_name), "SAVE_STATE_SEGMENT_%zu", cpu_index);
			if (smm_smram_carve_top(smram_free, needed_ss_size, 1, segment_name, &cpu_info->ss) != CB_SUCCESS)
				return 0;
			/* SMBASE start's SMM_CODE_SEGMENT_SIZE bytes before end of save state */
			cpu_info->smbase = region_last(&cpu_info->ss) + 1 - SMM_CODE_SEGMENT_SIZE;
			/* Stub code starts at SMBASE + SMM_ENTRY_OFFSET */
			cpu_info->stub_code = region_create(cpu_info->smbase + SMM_ENTRY_OFFSET, stub_size);
			cpu_info->active = 1;
		}

		for (size_t i = 0; i < cpus_per_segment; i++) {
			struct region stub_code;
			const size_t cpu_index = segment * cpus_per_segment + i;
			struct cpu_smm_info *cpu_info = &cpus[cpu_index];
			if (cpu_index >= num_cpus)
				break;

			const size_t len = region_last(smram_free) + 1 - region_offset(&cpu_info->stub_code);
			/* Reserve space for the stub code in SMRAM. Might be larger than the actual stub size
			 * due to size of save state. */
			snprintf(segment_name, sizeof(segment_name), "STUB_CODE_%zu", cpu_index);
			if (smm_smram_carve_top(smram_free, len, 1, segment_name, &stub_code) != CB_SUCCESS)
				return 0;

			/* Sanity check if calculated stub code region is within the reserved region */
			if (!region_is_subregion(&stub_code, &cpu_info->stub_code))
				die("stub code not within reserved region\n");
		}
	}

	return 1;
}

/*
 * This method expects the smm relocation map to be complete.
 * This method does not read any HW registers, it simply uses a
 * map that was created during SMM setup.
 * input: cpu_num - cpu number which is used as an index into the
 *       map to return the smbase
 */
u32 smm_get_cpu_smbase(unsigned int cpu_num)
{
	if (cpu_num < CONFIG_MAX_CPUS) {
		if (cpus[cpu_num].active)
			return cpus[cpu_num].smbase;
	}
	return 0;
}

/*
 * This method assumes that at least 1 CPU has been set up from
 * which it will place other CPUs below its smbase ensuring that
 * save state does not clobber the first CPUs init code segment. The init
 * code which is the smm stub code is the same for all CPUs. They enter
 * smm, setup stacks (based on their apic id), enter protected mode
 * and then jump to the common smi handler.  The stack is allocated
 * at the beginning of smram (aka tseg base, not smbase). The stack
 * pointer for each CPU is calculated by using its apic id
 * (code is in smm_stub.s)
 * Each entry point will now have the same stub code which, sets up the CPU
 * stack, enters protected mode and then jumps to the smi handler. It is
 * important to enter protected mode before the jump because the "jump to
 * address" might be larger than the 20bit address supported by real mode.
 * SMI entry right now is in real mode.
 * input: num_cpus - number of cpus that need relocation including
 *        the first CPU (though its code is already loaded)
 */

static void smm_place_entry_code(const unsigned int num_cpus)
{
	unsigned int i;
	size_t size;

	/* start at 1, the first CPU stub code is already there */
	size = region_sz(&cpus[0].stub_code);
	for (i = 1; i < num_cpus; i++) {
		printk(BIOS_DEBUG,
		       "SMM Module: placing smm entry code at %zx,  cpu # 0x%x\n",
		       region_offset(&cpus[i].stub_code), i);
		memcpy((void *)region_offset(&cpus[i].stub_code),
		       (void *)region_offset(&cpus[0].stub_code), size);
		printk(BIOS_SPEW, "%s: copying from %zx to %zx 0x%zx bytes\n",
		       __func__, region_offset(&cpus[0].stub_code),
		       region_offset(&cpus[i].stub_code), size);
	}
}

static uintptr_t stack_top;
static size_t g_stack_size;

int smm_setup_stack(const uintptr_t perm_smbase, const size_t perm_smram_size,
		     const unsigned int total_cpus, const size_t stack_size)
{
	/* Need a minimum stack size and alignment. */
	if (stack_size <= SMM_MINIMUM_STACK_SIZE || (stack_size & 3) != 0) {
		printk(BIOS_ERR, "%s: need minimum stack size\n", __func__);
		return -1;
	}

	const size_t total_stack_size = total_cpus * stack_size;
	if (total_stack_size >= perm_smram_size) {
		printk(BIOS_ERR, "%s: Stack won't fit smram\n", __func__);
		return -1;
	}
	stack_top = perm_smbase + total_stack_size;
	g_stack_size = stack_size;
	return 0;
}

/*
 * Place the staggered entry points for each CPU. The entry points are
 * staggered by the per CPU SMM save state size extending down from
 * SMM_ENTRY_OFFSET.
 */
static void smm_stub_place_staggered_entry_points(const struct smm_loader_params *params)
{
	if (params->num_concurrent_save_states > 1)
		smm_place_entry_code(params->num_concurrent_save_states);
}

/*
 * The stub setup code assumes it is completely contained within the
 * default SMRAM size (0x10000) for the default SMI handler (entry at
 * 0x30000), but no assumption should be made for the permanent SMI handler.
 * The placement of CPU entry points for permanent handler are determined
 * by the number of CPUs in the system and the amount of SMRAM.
 * There are potentially 2 regions to place
 * within the default SMRAM size:
 * 1. Save state areas
 * 2. Stub code
 *
 * The save state always lives at the top of the CPUS smbase (and the entry
 * point is at offset 0x8000). This allows only a certain number of CPUs with
 * staggered entry points until the save state area comes down far enough to
 * overwrite/corrupt the entry code (stub code). Therefore, an SMM map is
 * created to avoid this corruption, see smm_create_map() above.
 * This module setup code works for the default (0x30000) SMM handler setup and the
 * permanent SMM handler.
 * The CPU stack is decided at runtime in the stub and is treaded as a continuous
 * region. As this might not fit the default SMRAM region, the same region used
 * by the permanent handler can be used during relocation.
 */
static int smm_module_setup_stub(const uintptr_t smm_stub_loc, struct smm_loader_params *params)
{
	struct rmodule smm_stub;

	if (rmodule_parse(&_binary_smmstub_start, &smm_stub)) {
		printk(BIOS_ERR, "%s: unable to parse smm stub\n", __func__);
		return -1;
	}
	const size_t stub_size = rmodule_memory_size(&smm_stub);

	/* Some sanity check */
	if (stub_size >= SMM_ENTRY_OFFSET) {
		printk(BIOS_ERR, "%s: Stub too large\n", __func__);
		return -1;
	}

	if (rmodule_load((void *)smm_stub_loc, &smm_stub)) {
		printk(BIOS_ERR, "%s: load module failed\n", __func__);
		return -1;
	}

	struct smm_stub_params *stub_params = rmodule_parameters(&smm_stub);
	stub_params->stack_top = stack_top;
	stub_params->stack_size = g_stack_size;
	stub_params->c_handler = (uintptr_t)params->handler;
	stub_params->cr3 = params->cr3;
	stub_params->gdt_limit = params->gdt_limit;
	stub_params->gdt_base = params->gdt_base;

	/* This runs on the BSP. All the APs are its siblings */
	struct cpu_info *info = cpu_info();
	if (!info || !info->cpu) {
		printk(BIOS_ERR, "%s: Failed to find BSP struct device\n", __func__);
		return -1;
	}
	int i = 0;
	for (struct device *dev = info->cpu; dev; dev = dev->sibling)
		if (dev->enabled)
			stub_params->apic_id_to_cpu[i++] = dev->path.apic.initial_lapicid;

	if (i != params->num_cpus) {
		printk(BIOS_ERR, "%s: Failed to set up apic map correctly\n", __func__);
		return -1;
	}

	printk(BIOS_DEBUG, "%s: stack_top = 0x%x\n", __func__, stub_params->stack_top);
	printk(BIOS_DEBUG, "%s: per cpu stack_size = 0x%x\n", __func__,
	       stub_params->stack_size);

	smm_stub_place_staggered_entry_points(params);

	printk(BIOS_DEBUG, "SMM Module: stub loaded at %lx will call %p\n", smm_stub_loc,
	       params->handler);
	return 0;
}

/*
 * smm_setup_relocation_handler assumes the callback is already loaded in
 * memory. i.e. Another SMM module isn't chained to the stub. The other
 * assumption is that the stub will be entered from the default SMRAM
 * location: 0x30000 -> 0x40000.
 */
int smm_setup_relocation_handler(struct smm_loader_params *params)
{
	struct gdt_descriptor gdtr;

	uintptr_t smram = SMM_DEFAULT_BASE;
	printk(BIOS_SPEW, "%s: enter\n", __func__);
	/* There can't be more than 1 concurrent save state for the relocation
	 * handler because all CPUs default to 0x30000 as SMBASE. */
	if (params->num_concurrent_save_states > 1)
		return -1;

	/* A handler has to be defined to call for relocation. */
	if (params->handler == NULL)
		return -1;

	/* Since the relocation handler always uses stack, adjust the number
	 * of concurrent stack users to be CONFIG_MAX_CPUS. */
	if (params->num_cpus == 0)
		params->num_cpus = CONFIG_MAX_CPUS;

	/* Use current GDT for the relocation handler */
	read_gdtr(&gdtr);
	params->gdt_limit = gdtr.limit;
	params->gdt_base = gdtr.base;

	printk(BIOS_SPEW, "%s: exit\n", __func__);
	return smm_module_setup_stub(smram + SMM_ENTRY_OFFSET, params);
}

static void setup_smihandler_params(struct smm_runtime *mod_params,
				    struct smm_loader_params *loader_params)
{
	uintptr_t tseg_base;
	size_t tseg_size;

	smm_region(&tseg_base, &tseg_size);

	mod_params->smbase = tseg_base;
	mod_params->smm_size = tseg_size;
	mod_params->save_state_size = loader_params->cpu_save_state_size;
	mod_params->num_cpus = loader_params->num_cpus;
	mod_params->s3_resume = acpi_is_wakeup_s3();
	mod_params->gnvs_ptr = (uint32_t)(uintptr_t)acpi_get_gnvs();
	if (CONFIG(ACPI_SMM_GLOBAL_LOCK)) {
		acpi_facs_t *facs = acpi_get_facs();

		if (facs)
			mod_params->acpi_global_lock = (void *)((uintptr_t)facs +
							offsetof(acpi_facs_t, global_lock));
	}
	const struct cbmem_entry *cbmemc;
	if (CONFIG(CONSOLE_CBMEM) && (cbmemc = cbmem_entry_find(CBMEM_ID_CONSOLE))) {
		mod_params->cbmemc = cbmem_entry_start(cbmemc);
		mod_params->cbmemc_size = cbmem_entry_size(cbmemc);
	} else {
		mod_params->cbmemc = 0;
		mod_params->cbmemc_size = 0;
	}

	for (int i = 0; i < loader_params->num_cpus; i++)
		mod_params->save_state_top[i] = region_last(&cpus[i].ss) + 1;

	if (CONFIG(RUNTIME_CONFIGURABLE_SMM_LOGLEVEL))
		mod_params->smm_log_level = mainboard_set_smm_log_level();
	else
		mod_params->smm_log_level = 0;

	if (CONFIG(SMM_PCI_RESOURCE_STORE))
		smm_pci_resource_store_init(mod_params);

	if (CONFIG(SMMSTORE)) {
		uintptr_t base;
		size_t size;

		if (smmstore_init_com_buffer(&base, &size) < 0) {
			if (CONFIG(CDK2_S4_LAYOUT_REQUIRED))
				die("SMMSTORE: S4 mailbox unavailable\n");
			printk(BIOS_ERR, "SMMSTORE: Failed to allocate com buffer\n");
			return;
		}
		mod_params->smmstore_com_buffer_base = base;
		mod_params->smmstore_com_buffer_size = size;
	}

#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE)
	/* SMMSTORE has already reserved its S4 mailbox and raised the CBMEM floor. */
	const struct cbmem_entry *spi_console = cbmem_entry_add(CBMEM_ID_PAYLOAD_SPI_CONSOLE,
		PAYLOAD_SPI_CONSOLE_BUFFER_SIZE);
	uintptr_t spi_console_base = spi_console ? (uintptr_t)cbmem_entry_start(spi_console) : 0;

	if (spi_console && cbmem_entry_size(spi_console) == PAYLOAD_SPI_CONSOLE_BUFFER_SIZE &&
	    spi_console_base &&
	    spi_console_base <= UINT32_MAX - (PAYLOAD_SPI_CONSOLE_BUFFER_SIZE - 1U)) {
		mod_params->payload_spi_console_buffer_base = spi_console_base;
		mod_params->payload_spi_console_buffer_size = PAYLOAD_SPI_CONSOLE_BUFFER_SIZE;
	} else {
		printk(BIOS_ERR, "Payload SPI console: communication buffer unavailable\n");
	}
#endif

#if CONFIG(SMM_AUTHVAR)
	const struct cbmem_entry *authority = cbmem_entry_add(CBMEM_ID_AUTHVAR_BOOT_AUTHORITY,
		AUTHVAR_BOOT_AUTHORITY_BACKING_SIZE);
	uintptr_t authority_base = authority ? (uintptr_t)cbmem_entry_start(authority) : 0;

	if (!authority || cbmem_entry_size(authority) != AUTHVAR_BOOT_AUTHORITY_BACKING_SIZE ||
	    !authority_base || !IS_ALIGNED(authority_base, AUTHVAR_BOOT_AUTHORITY_PAGE_SIZE) ||
	    authority_base > UINT32_MAX - (AUTHVAR_BOOT_AUTHORITY_BACKING_SIZE - 1U))
		die("Authenticated variables: boot authority allocation failed\n");
	mod_params->authvar_boot_authority_base = authority_base;
	mod_params->authvar_boot_authority_size = AUTHVAR_BOOT_AUTHORITY_BACKING_SIZE;
#endif

#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
	void *staging = cbmem_add(CBMEM_ID_CAPSULE_STAGING, CONFIG_ROM_SIZE + 1 * MiB);

	if (!staging)
		die("Capsules: staging allocation failed\n");
	mod_params->capsule_staging_base = (uintptr_t)staging;
	mod_params->capsule_staging_size = CONFIG_ROM_SIZE + 1 * MiB;
#endif

#if CONFIG(SMM_OPAL_S3_SCRATCH_CBMEM)
	/*
	 * Provide a small, coreboot-managed CBMEM scratch region for SMM code.
	 * CBMEM is reserved in the memory map, and persists across S3, making
	 * it suitable for firmware-owned DMA buffers on resume. SMRAM is not
	 * accessible to devices for DMA, so this scratch buffer must live
	 * outside SMRAM.
	 */
	const size_t opal_s3_scratch_size = CONFIG_SMM_OPAL_S3_SCRATCH_SIZE;
	void *scratch = cbmem_add(CBMEM_ID_OPAL_S3_SCRATCH, opal_s3_scratch_size);
	if (!scratch) {
		printk(BIOS_ERR, "SMM: Failed to allocate OPAL S3 scratch\n");
		mod_params->opal_s3_scratch_base = 0;
		mod_params->opal_s3_scratch_size = 0;
	} else {
		mod_params->opal_s3_scratch_base = (uintptr_t)scratch;
		mod_params->opal_s3_scratch_size = opal_s3_scratch_size;
	}
#endif

#if CONFIG(SMM_OPAL_S3_STATE_SMRAM)
	uintptr_t state_base = 0;
	size_t state_size = 0;
	if (smm_subregion(SMM_SUBREGION_OPAL_S3_STATE, &state_base, &state_size)) {
		printk(BIOS_ERR, "SMM: Failed to locate OPAL S3 state SMRAM region\n");
		mod_params->opal_s3_state_base = 0;
		mod_params->opal_s3_state_size = 0;
	} else {
		mod_params->opal_s3_state_base = state_base;
		mod_params->opal_s3_state_size = state_size;

		/* Clear any stale state on cold boot/reboot, but preserve it on S3 resume. */
		if (!acpi_is_wakeup_s3())
			memset((void *)state_base, 0, state_size);
	}
#endif
}

/* Page table */
#define _PRES (1ULL << 0)
#define _RW   (1ULL << 1)
#define _US   (1ULL << 2)
#define _A    (1ULL << 5)
#define _D    (1ULL << 6)
#define _PS   (1ULL << 7)
#define _GEN_DIR(a) (_PRES + _RW + _US + _A + (a))
#define _GEN_PAGE(a) (_PRES + _RW + _US + _PS + _A +  _D + (a))
#define PTE_SIZE 8

/* Returns the size of the permanent page table for SMM */
__maybe_unused static enum cb_err smm_permanent_page_table_size(const struct region *smram_free, size_t *size, size_t *alignment)
{
	const bool one_g_pages = !!(cpuid_edx(0x80000001) & (1 << 26));

	/*
	 * CONFIG_CPU_PT_ROM_MAP_GB 1G pages or
	 * CONFIG_CPU_PT_ROM_MAP_GB PDPE entries with 512 * 2M pages
	 */
	const size_t ptes_needed = CONFIG_CPU_PT_ROM_MAP_GB + (one_g_pages ? 0 :
				   512 * CONFIG_CPU_PT_ROM_MAP_GB);

	/* One PML4E entry plus the required PTEs */
	*size = 4096 + ALIGN_UP(ptes_needed * PTE_SIZE, 4096);
	*alignment = 4096;
	return CB_SUCCESS;
}

/* Installs page tables at given base address. Caller has made sure that there is enough space. */
__maybe_unused static enum cb_err smm_permanent_install_page_table(const struct region *region, struct smm_loader_params *params)
{
	const bool one_g_pages = !!(cpuid_edx(0x80000001) & (1 << 26));

	const uintptr_t pml4e = region_offset(region);
	const uintptr_t pages_base = pml4e + 4096;
	uintptr_t pdpt;

	if (one_g_pages) {
		for (size_t i = 0; i < CONFIG_CPU_PT_ROM_MAP_GB; i++)
			write64p(pages_base + i * PTE_SIZE, _GEN_PAGE(1ull * GiB * i));
		pdpt = pages_base;
	} else {
		for (size_t i = 0; i < 512 * CONFIG_CPU_PT_ROM_MAP_GB; i++)
			write64p(pages_base + i * PTE_SIZE, _GEN_PAGE(2ull * MiB * i));

		pdpt = pages_base + 4096 * CONFIG_CPU_PT_ROM_MAP_GB;
		for (size_t i = 0; i < CONFIG_CPU_PT_ROM_MAP_GB; i++)
			write64p(pdpt + i * PTE_SIZE, _GEN_DIR(pages_base + 4096 * i));
	}

	for (size_t i = 0; i < DIV_ROUND_UP(CONFIG_CPU_PT_ROM_MAP_GB, 512); i++)
		write64p(pml4e + i * PTE_SIZE, _GEN_DIR(pdpt + i * 4096));

	params->cr3 = pml4e;
	return CB_SUCCESS;
}

/* STM */
__maybe_unused static enum cb_err smm_stm_get_size_and_alignment(const struct region *smram_free,
								 size_t *size, size_t *alignment)
{
	*size = CONFIG_MSEG_SIZE + CONFIG_BIOS_RESOURCE_LIST_SIZE;
	*alignment = 1;
	return CB_SUCCESS;
}

__maybe_unused static enum cb_err smm_stm_install(const struct region *region,
						  struct smm_loader_params *params)
{
	(void)region; (void)params;

	printk(BIOS_DEBUG, "MSEG size     0x%x\n", CONFIG_MSEG_SIZE);
	printk(BIOS_DEBUG, "BIOS res list 0x%x\n", CONFIG_BIOS_RESOURCE_LIST_SIZE);

	return CB_SUCCESS;
}

/* GDT */
static const struct gdt_entry permanent_gdt[] = {
	[0] = { 0 }, /* Empty to keep NULL selector */
	[1] = { /* gdt selector 0x08, flat code segment */
		.limit_low = 0xffff,
		.access = 0x9b,
		.limit_high_and_flags = 0xcf,
	},
	[2] = { /* gdt selector 0x10, flat data segment */

		.limit_low = 0xffff,
		.access = 0x93,
		.limit_high_and_flags = 0xcf,
	},
	[3] = { /* gdt selector 0x18, flat code segment (64-bit) */

		.limit_low = 0xffff,
		.access = 0x9b,
		.limit_high_and_flags = 0xaf,
	},
	[4] = { /* gdt selector 0x20 tss segment */
		.limit_low = 0xffff,
		.access = 0x8b,
		.limit_high_and_flags = 0x80,
	},
};

static enum cb_err smm_permanent_gdt_size(const struct region *smram_free,
					  size_t *size, size_t *alignment)
{
	*size = sizeof(permanent_gdt);
	*alignment = 8;
	return CB_SUCCESS;
}

static enum cb_err smm_install_permanent_gdt(const struct region *region,
					     struct smm_loader_params *params)
{
	memcpy((void *)(uintptr_t)region_offset(region), permanent_gdt, sizeof(permanent_gdt));

	params->gdt_base = region_offset(region);
	params->gdt_limit = region_sz(region) - 1;
	return CB_SUCCESS;
}

/* Permanent SMI handler */
struct rmodule smi_handler;
static enum cb_err smm_handler_get_size_and_alignment(const struct region *smram_free,
						      size_t *size, size_t *alignment)
{
	if (rmodule_parse(&_binary_smm_start, &smi_handler))
		return CB_ERR;

	*size = rmodule_memory_size(&smi_handler);
	*alignment = rmodule_load_alignment(&smi_handler);

	return CB_SUCCESS;
}

static enum cb_err smm_handler_install_handler(const struct region *region,
					       struct smm_loader_params *params)
{
	if (rmodule_load((void *)region_offset(region), &smi_handler))
		return -1;
	if (CONFIG(SMM_AUTHVAR)) {
		const struct rmodule_header *header = smi_handler.header;
		const size_t link = header->module_link_start_address;
		const size_t bss_end = header->bss_end;
		const size_t handler_size = rmodule_memory_size(&smi_handler);

		/* The owner tail survives S3; ordinary BSS was reset by the loader. */
		if (bss_end < link || bss_end - link >= handler_size ||
		    bss_end - link < (size_t)smi_handler.payload_size)
			return CB_ERR;
		if (!acpi_is_wakeup_s3())
			memset((void *)(region_offset(region) + bss_end - link), 0,
				handler_size - (bss_end - link));
	}
	params->handler = rmodule_entry(&smi_handler);

	return CB_SUCCESS;
}

static const struct smram_regions {
	const char *name;
	enum cb_err (* get_size_and_alignment)(const struct region *smram_free,
					       size_t *size, size_t *alignment);
	enum cb_err (* install)(const struct region *region,
				struct smm_loader_params *params);
} regions[] = {
#if CONFIG(STM)
	{
		.name = "STM",
		.get_size_and_alignment = smm_stm_get_size_and_alignment,
		.install = smm_stm_install,
	},
#endif
	{
		.name = "GDT",
		.get_size_and_alignment = smm_permanent_gdt_size,
		.install = smm_install_permanent_gdt,
	},
#if ENV_X86_64
	{
		.name = "PERMANENT PAGE TABLES",
		.get_size_and_alignment = smm_permanent_page_table_size,
		.install = smm_permanent_install_page_table,
	},
#endif
	{
		.name = "HANDLER",
		.get_size_and_alignment = smm_handler_get_size_and_alignment,
		.install = smm_handler_install_handler,
	}
};

/*
 *The SMM module is placed within the provided region in the following
 * manner:
 * +-----------------+ <- smram + size
 * | BIOS resource   |
 * | list (STM)      |
 * +-----------------+
 * |  smi handler    |
 * |      ...        |
 * +-----------------+
 * |  page tables    |
 * +-----------------+ <- cpu0
 * |    stub code    | <- cpu1
 * |    stub code    | <- cpu2
 * |    stub code    | <- cpu3, etc
 * |                 |
 * |                 |
 * |-----------------|
 * |    unused       |
 * |-----------------| <- num_concurrent_save_states * CONFIG_SMM_MODULE_STACK_SIZE
 * |    stacks       |
 * +-----------------+ <- smram start
 *
 * With CONFIG(SMM_TSEG) the stubs will be placed in the same segment as the
 * permanent handler and the stacks.
 */
int smm_load_module(const uintptr_t smram_base, const size_t smram_size,
		    struct smm_loader_params *params)
{
	const struct region smram = region_create(smram_base, smram_size);
	struct region smram_free = smram;

	/* Collect regions and allocate them */
	for (size_t i = 0; i < ARRAY_SIZE(regions); i++) {
		struct region region = {};
		size_t size = 0, alignment = 0;
		if (regions[i].get_size_and_alignment(&smram_free, &size, &alignment) != CB_SUCCESS)
			return -1;
		if (smm_smram_carve_top(&smram_free, size, alignment, regions[i].name, &region) != CB_SUCCESS)
			return -1;
		if (regions[i].install(&region, params) != CB_SUCCESS)
			return -1;
	}

	/* Fill in save states and stub code into cpus array */
	if (!smm_create_map(&smram_free, params->num_cpus, params->cpu_save_state_size)) {
		printk(BIOS_ERR, "%s: Error creating CPU map\n", __func__);
		return -1;
	}

	/* Check if there is enough space for SMM module stacks */
	if (region_sz(&smram_free) < params->num_concurrent_save_states * CONFIG_SMM_MODULE_STACK_SIZE) {
		printk(BIOS_ERR, "%s: Not enough SMRAM for SMM module stacks\n", __func__);
		return -1;
	}

	struct smm_runtime *smihandler_params = rmodule_parameters(&smi_handler);
	setup_smihandler_params(smihandler_params, params);

	return smm_module_setup_stub(region_offset(&cpus[0].stub_code), params);
}
