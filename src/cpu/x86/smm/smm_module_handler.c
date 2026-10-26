/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <arch/exception.h>
#include <arch/cpu.h>
#include <commonlib/region.h>
#include <console/cbmem_console.h>
#include <console/console.h>
#include <console/uart.h>
#include <cpu/cpu.h>
#include <cpu/x86/smm.h>
#include <cpu/x86/msr.h>
#include <cpu/x86/mtrr.h>
#include <rmodule.h>
#include <types.h>
#include <security/intel/stm/SmmStm.h>
#if CONFIG(SMM_AUTHVAR)
#include <intelblocks/msr.h>
#endif

#if CONFIG(SPI_FLASH_SMM)
#include <spi-generic.h>
#endif

static int do_driver_init = 1;

/* Odd values identify an active SMI; even values mean the handler is unlocked. */
static unsigned int smi_handler_status;
static unsigned int cpu_smi_generation[CONFIG_MAX_CPUS];
static uint32_t smi_request_epoch;
static uint32_t smi_parked_epoch[CONFIG_MAX_CPUS];
#if CONFIG(SMM_AUTHVAR)
static bool smi_smrr_valid[CONFIG_MAX_CPUS];

static bool smrr_valid(void)
{
	uintptr_t base;
	size_t size;
	msr_t cap = rdmsr(MTRR_CAP_MSR);
	msr_t smrr_base, mask;
	uint32_t expected_mask;

	smm_region(&base, &size);
	if (!(cap.lo & MTRR_CAP_SMRR) || !size || !IS_POWER_OF_2(size) ||
	    !IS_ALIGNED(base, size) || base > UINT32_MAX || size - 1U > UINT32_MAX - base)
		return false;
	smrr_base = rdmsr(IA32_SMRR_PHYS_BASE);
	mask = rdmsr(IA32_SMRR_PHYS_MASK);
	expected_mask = (~(size - 1U) & ~0xfffU) | MTRR_PHYS_MASK_VALID;
	if (cap.lo & SMRR_LOCK_SUPPORTED)
		expected_mask |= SMRR_PHYS_MASK_LOCK;
	return !smrr_base.hi && !mask.hi &&
		smrr_base.lo == (base | MTRR_TYPE_WRBACK) && mask.lo == expected_mask;
}
#endif
static int smi_leader = -1;

static const volatile
__attribute((aligned(4), __section__(".module_parameters"))) struct smm_runtime smm_runtime;

static bool smi_obtain_lock(unsigned int *generation)
{
	unsigned int state = __atomic_load_n(&smi_handler_status, __ATOMIC_ACQUIRE);

	while (!(state & 1)) {
		if (__atomic_compare_exchange_n(&smi_handler_status, &state, state + 1,
				false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
			*generation = state + 1;
			return true;
		}
	}

	*generation = state;
	return false;
}

static void smi_release_lock(void)
{
	__atomic_fetch_add(&smi_handler_status, 1, __ATOMIC_RELEASE);
}

bool smm_wait_for_cpus(void)
{
	const unsigned int count = smm_runtime.num_cpus;
	uint32_t epoch = __atomic_load_n(&smi_request_epoch, __ATOMIC_RELAXED);
	/* Bound polling even on SMM builds without a monotonic timer. */
	unsigned int polls = 1000000;

	if (!count || count > CONFIG_MAX_CPUS || smi_leader < 0 ||
	    (unsigned int)smi_leader >= count || epoch == UINT32_MAX ||
	    !(__atomic_load_n(&smi_handler_status, __ATOMIC_RELAXED) & 1))
		return false;
	/* Never reuse an epoch: an old arrival must not authorize a new write. */
	epoch++;
#if CONFIG(SMM_AUTHVAR)
	if (!smrr_valid())
		return false;
#endif
	__atomic_store_n(&smi_request_epoch, epoch, __ATOMIC_RELEASE);
	for (unsigned int cpu = 0; cpu < count; cpu++) {
		if (cpu == (unsigned int)smi_leader)
			continue;
		while (__atomic_load_n(&smi_parked_epoch[cpu], __ATOMIC_ACQUIRE) != epoch) {
			if (!polls--)
				return false;
			cpu_relax();
		}
#if CONFIG(SMM_AUTHVAR)
		if (!smi_smrr_valid[cpu])
			return false;
#endif
	}
	return true;
}

#if CONFIG(RUNTIME_CONFIGURABLE_SMM_LOGLEVEL)
int get_console_loglevel(void)
{
	return smm_runtime.smm_log_level;
}
#endif

#define ACPI_GLOBAL_LOCK_PENDING (1U << 0)
#define ACPI_GLOBAL_LOCK_OWNED (1U << 1)

bool smm_acpi_global_lock_acquire(void)
{
	uint32_t *lock = smm_runtime.acpi_global_lock;

	if (!lock)
		return false;

	uint32_t value = __atomic_load_n(lock, __ATOMIC_RELAXED);

	if (value & ACPI_GLOBAL_LOCK_OWNED)
		return false;

	/* Do not set Pending: SMM must not wait for the interrupted OS. */
	return __atomic_compare_exchange_n(lock, &value,
		value | ACPI_GLOBAL_LOCK_OWNED, false, __ATOMIC_ACQUIRE,
		__ATOMIC_RELAXED);
}

bool smm_acpi_global_lock_release(void)
{
	uint32_t value = __atomic_fetch_and(smm_runtime.acpi_global_lock,
		~(ACPI_GLOBAL_LOCK_PENDING | ACPI_GLOBAL_LOCK_OWNED), __ATOMIC_RELEASE);

	return value & ACPI_GLOBAL_LOCK_PENDING;
}

bool smm_is_s3_resume(void)
{
	return smm_runtime.s3_resume;
}

#if CONFIG(PAYLOAD_SPI_FLASH_CONSOLE)
void smm_get_payload_spi_console_buffer(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.payload_spi_console_buffer_base;
	*size = smm_runtime.payload_spi_console_buffer_size;
}
#endif

void smm_get_smmstore_com_buffer(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.smmstore_com_buffer_base;
	*size = smm_runtime.smmstore_com_buffer_size;
}

#if CONFIG(DRIVERS_EFI_CAPSULE_TYPED_TRANSPORT)
void smm_get_capsule_staging(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.capsule_staging_base;
	*size = smm_runtime.capsule_staging_size;
}
#endif

#if CONFIG(SMM_AUTHVAR)
void smm_get_authvar_boot_authority(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.authvar_boot_authority_base;
	*size = smm_runtime.authvar_boot_authority_size;
}
#endif

#if CONFIG(SMM_OPAL_S3_SCRATCH_CBMEM)
void smm_get_opal_s3_scratch_buffer(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.opal_s3_scratch_base;
	*size = smm_runtime.opal_s3_scratch_size;
}
#endif

#if CONFIG(SMM_OPAL_S3_STATE_SMRAM)
void smm_get_opal_s3_state_buffer(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.opal_s3_state_base;
	*size = smm_runtime.opal_s3_state_size;
}
#endif

void smm_get_cbmemc_buffer(void **buffer_out, size_t *size_out)
{
	*buffer_out = smm_runtime.cbmemc;
	*size_out = smm_runtime.cbmemc_size;
}

void smm_region(uintptr_t *start, size_t *size)
{
	*start = smm_runtime.smbase;
	*size = smm_runtime.smm_size;
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

struct global_nvs *gnvs;

void *smm_get_save_state(int cpu)
{
	if (cpu >= smm_runtime.num_cpus)
		return NULL;

	return (void *)(smm_runtime.save_state_top[cpu] -
			(smm_runtime.save_state_size - STM_PSD_SIZE));
}

void *smm_get_current_save_state(unsigned int cpu)
{
	unsigned int generation = __atomic_load_n(&smi_handler_status, __ATOMIC_ACQUIRE);

	if (cpu >= CONFIG_MAX_CPUS ||
	    __atomic_load_n(&cpu_smi_generation[cpu], __ATOMIC_ACQUIRE) != generation)
		return NULL;

	return smm_get_save_state(cpu);
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
	unsigned int generation;
	uintptr_t actual_canary;
	uintptr_t expected_canary;

	p = arg;
	cpu = p->cpu;
	expected_canary = (uintptr_t)p->canary;

	/* Make sure to set the global runtime. It's OK to race as the value
	 * will be the same across CPUs as well as multiple SMIs. */
	gnvs = (void *)(uintptr_t)smm_runtime.gnvs_ptr;

	if (cpu < 0 || cpu >= CONFIG_MAX_CPUS) {
		/* Do not log messages to console here, it is not thread safe */
		return;
	}

	/* Publish entry only after hardware has written this CPU's save state. */
	bool handler = smi_obtain_lock(&generation);
	__atomic_store_n(&cpu_smi_generation[cpu], generation, __ATOMIC_RELEASE);

	if (!handler) {
		uint32_t acknowledged_epoch = 0;

		/* Keep followers in SMM until the handler releases the lock. */
		while (__atomic_load_n(&smi_handler_status, __ATOMIC_ACQUIRE) & 1) {
			const uint32_t epoch = __atomic_load_n(&smi_request_epoch,
				__ATOMIC_ACQUIRE);

			if (epoch != acknowledged_epoch) {
#if CONFIG(SMM_AUTHVAR)
				smi_smrr_valid[cpu] = smrr_valid();
#endif
				__atomic_store_n(&smi_parked_epoch[cpu], epoch, __ATOMIC_RELEASE);
				acknowledged_epoch = epoch;
			}
			cpu_relax();
		}
		__atomic_store_n(&cpu_smi_generation[cpu], 0, __ATOMIC_RELEASE);
		return;
	}
	smi_leader = cpu;

	smi_backup_pci_address();

	smm_soc_early_init();

	console_init();

	printk(BIOS_SPEW, "\nSMI# #%d\n", cpu);

	if (CONFIG(DEBUG_SMI) && console_serial_enabled())
		exception_init();

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

	smm_soc_exit();

	__atomic_store_n(&cpu_smi_generation[cpu], 0, __ATOMIC_RELEASE);
	smi_leader = -1;
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
void __weak mainboard_smi_sleep_finalize(u8 slp_typ) {}
int __weak mainboard_smi_finalize(void) { return 0; }

void __weak smm_soc_early_init(void) {}
void __weak smm_soc_exit(void) {}
