/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/cpu.h>
#include <arch/exception.h>
#include <arch/io.h>
#include <arch/null_breakpoint.h>
#include <arch/stack_canary_breakpoint.h>
#include <commonlib/region.h>
#include <console/cbmem_console.h>
#include <console/console.h>
#include <cpu/cpu.h>
#include <cpu/x86/gdt.h>
#include <cpu/x86/lapic.h>
#include <cpu/x86/mp.h>
#include <cpu/x86/smm.h>
#include <rmodule.h>
#include <types.h>
#include <security/intel/stm/SmmStm.h>
#include "../mp_internal.h"

#if CONFIG(SPI_FLASH_SMM)
#include <spi-generic.h>
#endif

static int do_driver_init = 1;

typedef enum { SMI_LOCKED, SMI_UNLOCKED } smi_semaphore;

/* SMI multiprocessing semaphore */
static volatile
__attribute__((aligned(4))) smi_semaphore smi_handler_status = SMI_UNLOCKED;

static const volatile
__attribute((aligned(4), __section__(".module_parameters"))) struct smm_runtime smm_runtime;

static int smi_obtain_lock(void)
{
	u8 ret = SMI_LOCKED;

	asm volatile (
		"movb %2, %%al\n"
		"xchgb %%al, %1\n"
		"movb %%al, %0\n"
		: "=g" (ret), "=m" (smi_handler_status)
		: "g" (SMI_LOCKED)
		: "eax"
	);

	return (ret == SMI_UNLOCKED);
}

static void smi_release_lock(void)
{
	asm volatile (
		"movb %1, %%al\n"
		"xchgb %%al, %0\n"
		: "=m" (smi_handler_status)
		: "g" (SMI_UNLOCKED)
		: "eax"
	);
}

static const u16 *apic_id_to_cpu_num = NULL;

unsigned long cpu_index(void)
{
	unsigned int cpu_lapicid = initial_lapicid();

	for (int i = 0; i < smm_runtime.num_cpus && i < CONFIG_MAX_CPUS; i++) {
		if (apic_id_to_cpu_num[i] == cpu_lapicid)
			return i;
	}

	return -1;
}

int mp_internal_get_num_cpus(void)
{
	return smm_runtime.num_cpus;
}

#if CONFIG(RUNTIME_CONFIGURABLE_SMM_LOGLEVEL)
int get_console_loglevel(void)
{
	return smm_runtime.smm_log_level;
}
#endif

void smm_get_smmstore_com_buffer(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.smmstore_com_buffer_base;
	*size = smm_runtime.smmstore_com_buffer_size;
}

void smm_get_cbmemc_buffer(void **buffer_out, size_t *size_out)
{
	*buffer_out = smm_runtime.cbmemc;
	*size_out = smm_runtime.cbmemc_size;
}

void io_trap_handler(int smif)
{
	/* If a handler function handled a given IO trap, it
	 * shall return a non-zero value
	 */
	printk(BIOS_DEBUG, "SMI function trap 0x%x: ", smif);

	if (mainboard_io_trap_handler(smif))
		return;

	printk(BIOS_DEBUG, "Unknown function\n");
}

static u32 pci_orig;

/**
 * @brief Backup PCI address to make sure we do not mess up the OS
 */
static void smi_backup_pci_address(void)
{
	pci_orig = inl(0xcf8);
}

/**
 * @brief Restore PCI address previously backed up
 */
static void smi_restore_pci_address(void)
{
	outl(pci_orig, 0xcf8);
}

struct x86_debug_register_state {
	uintptr_t dr0, dr1, dr2, dr3;
};

static struct x86_debug_register_state s_x86_debug_register_state[CONFIG_MAX_CPUS];

static void x86_debug_register_save(void *unused)
{
	unsigned long cpu_idx = cpu_index();

	asm("mov %%dr0, %0" : "=r"(s_x86_debug_register_state[cpu_idx].dr0));
	asm("mov %%dr1, %0" : "=r"(s_x86_debug_register_state[cpu_idx].dr1));
	asm("mov %%dr2, %0" : "=r"(s_x86_debug_register_state[cpu_idx].dr2));
	asm("mov %%dr3, %0" : "=r"(s_x86_debug_register_state[cpu_idx].dr3));
}

static void x86_debug_register_restore(void *unused)
{
	unsigned long cpu_idx = cpu_index();

	asm("mov %0, %%dr0" :: "r"(s_x86_debug_register_state[cpu_idx].dr0));
	asm("mov %0, %%dr1" :: "r"(s_x86_debug_register_state[cpu_idx].dr1));
	asm("mov %0, %%dr2" :: "r"(s_x86_debug_register_state[cpu_idx].dr2));
	asm("mov %0, %%dr3" :: "r"(s_x86_debug_register_state[cpu_idx].dr3));
}

static void smm_exception_ap_init(void *unused)
{
	asm volatile (
		"lidt   %0"
		:
		: "m" (idtarg)
		: "memory"
	);
}

struct global_nvs *gnvs;

void *smm_get_save_state(int cpu)
{
	if (cpu >= smm_runtime.num_cpus)
		return NULL;

	return (void *)(smm_runtime.save_state_top[cpu] -
			(smm_runtime.save_state_size - STM_PSD_SIZE));
}

uint32_t smm_revision(void)
{
	const uintptr_t save_state = (uintptr_t)(smm_get_save_state(0));

	return *(uint32_t *)(save_state + smm_runtime.save_state_size
			     - SMM_REVISION_OFFSET_FROM_TOP);
}

bool smm_region_overlaps_handler(const struct region *r)
{
	const struct region r_smm = region_create(smm_runtime.smbase, smm_runtime.smm_size);
	const struct region r_aseg = region_create(SMM_BASE, SMM_DEFAULT_SIZE);

	return region_overlap(&r_smm, r) || region_overlap(&r_aseg, r);
}

asmlinkage void smm_handler_start(void *arg)
{
	const struct smm_module_params *p;
	int cpu;
	uintptr_t actual_canary;
	uintptr_t expected_canary;

	p = arg;
	cpu = p->cpu;
	expected_canary = (uintptr_t)p->canary;

	/* Make sure to set the global runtime. It's OK to race as the value
	 * will be the same across CPUs as well as multiple SMIs. */
	apic_id_to_cpu_num = p->apic_id_to_cpu;
	gnvs = (void *)(uintptr_t)smm_runtime.gnvs_ptr;

	if (cpu >= CONFIG_MAX_CPUS) {
		/* Do not log messages to console here, it is not thread safe */
		return;
	}

	/* Are we ok to execute the handler? */
	if (!smi_obtain_lock()) {
		mp_internal_ap_ready_for_instruction(cpu);

		/* For security reasons we don't release the other CPUs
		 * until the CPU with the lock is actually done */
		while (smi_handler_status == SMI_LOCKED) {
			mp_internal_ap_check_for_instruction(cpu);
			asm volatile ("pause");
		}
		return;
	}

	smi_backup_pci_address();

	smm_soc_early_init();

	console_init();

	printk(BIOS_SPEW, "\nSMI# #%d\n", cpu);

	if (CONFIG(DEBUG_SMI) && CONFIG(CONSOLE_SERIAL)) {
		mp_run_on_all_cpus(x86_debug_register_save, NULL);
		exception_init();
		mp_run_on_all_aps(smm_exception_ap_init, NULL, 1000, true);
	}

	/* Allow drivers to initialize variables in SMM context. */
	if (do_driver_init) {
#if CONFIG(SPI_FLASH_SMM)
		spi_init();
#endif
		do_driver_init = 0;
	}

	cpu_smi_handler();
	northbridge_smi_handler();
	southbridge_smi_handler();

	smi_restore_pci_address();

	actual_canary = *p->canary;

	if (actual_canary != expected_canary) {
		printk(BIOS_DEBUG, "canary 0x%lx != 0x%lx\n", actual_canary,
		       expected_canary);

		// Don't die if we can't indicate an error.
		if (CONFIG(DEBUG_SMI))
			die("SMM Handler caused a stack overflow\n");
	}

	if (CONFIG(DEBUG_SMI) && CONFIG(CONSOLE_SERIAL)) {
		// Clear out the allocated breakpoints so that we don't 'run out'
		null_breakpoint_remove();
		//stack_canary_breakpoint_remove();
		mp_run_on_all_cpus(x86_debug_register_restore, NULL);
	}

	smm_soc_exit();

	smi_release_lock();

	/* De-assert SMI# signal to allow another SMI */
	southbridge_smi_set_eos();
}

#if CONFIG(SMM_PCI_RESOURCE_STORE)
const volatile struct smm_pci_resource_info *smm_get_pci_resource_store(void)
{
	return &smm_runtime.pci_resources[0];
}
#endif

RMODULE_ENTRY(smm_handler_start);

/* Provide a default implementation for all weak handlers so that relocation
 * entries in the modules make sense. Without default implementations the
 * weak relocations w/o a symbol have a 0 address which is where the modules
 * are linked at. */
int __weak mainboard_io_trap_handler(int smif) { return 0; }
void __weak cpu_smi_handler(void) {}
void __weak northbridge_smi_handler(void) {}
void __weak southbridge_smi_handler(void) {}
void __weak mainboard_smi_gpi(u32 gpi_sts) {}
int __weak mainboard_smi_apmc(u8 data) { return 0; }
void __weak mainboard_smi_sleep(u8 slp_typ) {}
void __weak mainboard_smi_finalize(void) {}

void __weak smm_soc_early_init(void) {}
void __weak smm_soc_exit(void) {}
