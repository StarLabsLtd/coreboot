/* SPDX-License-Identifier: GPL-2.0-only */

#include <arch/io.h>
#include <boot/capsule_broker_buffers.h>
#include <arch/exception.h>
#include <commonlib/region.h>
#include <console/cbmem_console.h>
#include <console/console.h>
#include <cpu/cpu.h>
#if CONFIG(STM)
#include <cpu/x86/save_state.h>
#endif
#include <cpu/x86/smm.h>
#if CONFIG(SMM_INVOCATION_AUXILIARY_CHANNELS)
#include "../smm_invocation_auxiliary_channels_private.h"
#endif
#if CONFIG(SMM_PRE_LOCK_DISPATCH)
#include <cpu/x86/smm_pre_lock_dispatch.h>
#endif
#if CONFIG(SMM_INVOCATION_RUNTIME_VIEW)
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_save_state.h>
#endif
#if CONFIG(SMM_INVOCATION_STACK_CANARY_FAIL_STOP) || CONFIG(SMM_PRE_LOCK_DISPATCH)
#include <cpu/x86/smm_invocation_fail_stop.h>
#endif
#if CONFIG(STM)
#include <cpu/x86/smm_save_state.h>
#endif
#include <rmodule.h>
#include <types.h>
#include <security/intel/stm/SmmStm.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP)
#include <boot/payload_mm_authvar_smm_loader.h>
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI)
#include <boot/payload_mm_authvar_mor_private_smi.h>
#endif

#if CONFIG(SPI_FLASH_SMM)
#include <spi-generic.h>
#endif

static int do_driver_init = 1;

typedef enum { SMI_LOCKED, SMI_UNLOCKED } smi_semaphore;

/* SMI multiprocessing semaphore */
static volatile
__attribute__((aligned(4))) smi_semaphore smi_handler_status = SMI_UNLOCKED;

#if CONFIG(SMM_INVOCATION_TOPOLOGY)
#define SMM_RUNTIME_ALIGNMENT 8
#else
#define SMM_RUNTIME_ALIGNMENT 4
#endif
#if CONFIG(SMM_INVOCATION_RUNTIME_VIEW)
_Static_assert(_Alignof(uintptr_t) <= SMM_RUNTIME_ALIGNMENT,
	"SMM runtime does not align pointer members");
#endif
static const volatile
__attribute((aligned(SMM_RUNTIME_ALIGNMENT), __section__(".module_parameters")))
	struct smm_runtime smm_runtime;

#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
static bool spans_overlap(uintptr_t first, size_t first_size,
	uintptr_t second, size_t second_size)
{
	return first <= second ? second - first < first_size :
		first - second < second_size;
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_AUTHORITY)
struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *
	smm_get_payload_mm_authvar_presence_lifecycle_close_mailbox_authority(void)
{
	return (struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *)
		&smm_runtime.authvar_lifecycle_mailbox;
}
#endif

#if CONFIG(SMM_INVOCATION_RUNTIME_VIEW)
#if defined(__TEST__)
void smm_invocation_runtime_view_test_hook(uint32_t point);
#define RUNTIME_VIEW_TEST_HOOK(point) smm_invocation_runtime_view_test_hook(point)
#define RUNTIME_VIEW_RESERVED_SIZE TEST_RUNTIME_VIEW_RESERVED_SIZE
#else
#define RUNTIME_VIEW_TEST_HOOK(point) do { } while (0)
#define RUNTIME_VIEW_RESERVED_SIZE STM_PSD_SIZE
#endif

struct smm_invocation_runtime_view {
	uint8_t opaque;
};

static const struct smm_invocation_runtime_view runtime_view;

struct runtime_geometry_snapshot {
	uint32_t runtime_cpus;
	uint32_t allocation_size;
	uintptr_t smram_base;
	size_t smram_size;
};

static bool runtime_range_valid(uintptr_t base, size_t size, size_t alignment)
{
	return base && size && !(base % alignment) &&
		base <= UINTPTR_MAX - (size - 1U);
}

static bool runtime_ranges_overlap(uintptr_t first, size_t first_size,
	uintptr_t second, size_t second_size)
{
	if (first <= second)
		return second - first < first_size;
	return first - second < second_size;
}

static bool runtime_range_contains(uintptr_t outer, size_t outer_size,
	uintptr_t inner, size_t inner_size)
{
	return runtime_range_valid(outer, outer_size, 1U) &&
		runtime_range_valid(inner, inner_size, 1U) && inner >= outer &&
		inner - outer < outer_size &&
		inner_size <= outer_size - (inner - outer);
}

static __noinline bool runtime_topology_matches(uint32_t runtime_cpus)
{
	const volatile struct smm_invocation_topology *topology =
		&smm_runtime.invocation_topology;

	if (__atomic_load_n(&topology->state, __ATOMIC_ACQUIRE) !=
		SMM_INVOCATION_TOPOLOGY_READY ||
	    topology->revision != SMM_INVOCATION_TOPOLOGY_REVISION ||
	    topology->size != sizeof(*topology) ||
	    topology->active_cpus != runtime_cpus || topology->bsp_cpu != 0U ||
	    topology->reserved[0] || topology->reserved[1] ||
	    topology->reserved[2])
		return false;
	for (uint32_t cpu = 0; cpu < runtime_cpus; cpu++)
		for (uint32_t other = cpu + 1U; other < runtime_cpus; other++)
			if (topology->initial_apic_ids[cpu] ==
			    topology->initial_apic_ids[other])
				return false;
	for (uint32_t cpu = runtime_cpus;
	     cpu < SMM_INVOCATION_TOPOLOGY_MAX_CPUS; cpu++)
		if (topology->initial_apic_ids[cpu])
			return false;
	return __atomic_load_n(&topology->state, __ATOMIC_ACQUIRE) ==
		SMM_INVOCATION_TOPOLOGY_READY;
}

static __noinline bool runtime_composition_matches(void)
{
	const volatile struct smm_invocation_loader_composition *composition =
		&smm_runtime.invocation_composition;

	return __atomic_load_n(&composition->state, __ATOMIC_ACQUIRE) ==
			SMM_INVOCATION_LOADER_COMPOSITION_READY &&
		composition->owner_attempt == 1U && !composition->reserved[0] &&
		!composition->reserved[1] &&
		composition->evidence_identity ==
			(uint64_t)(uintptr_t)&smm_runtime.invocation_evidence &&
		__atomic_load_n(&composition->state, __ATOMIC_ACQUIRE) ==
			SMM_INVOCATION_LOADER_COMPOSITION_READY;
}

static __noinline bool runtime_geometry_valid(uint32_t runtime_cpus,
	uint32_t allocation_size,
	uintptr_t smram_base, size_t smram_size, uintptr_t output, size_t output_size,
	uintptr_t runtime, size_t runtime_size)
{
	struct smm_save_state_span native_span;
	for (uint32_t cpu = 0; cpu < runtime_cpus; cpu++) {
		const uintptr_t top = __atomic_load_n(&smm_runtime.save_state_top[cpu],
			__ATOMIC_ACQUIRE);
		uintptr_t allocation_base;

		RUNTIME_VIEW_TEST_HOOK(10U + cpu);
		if (top != __atomic_load_n(&smm_runtime.save_state_top[cpu],
			__ATOMIC_ACQUIRE) || top < allocation_size)
			return false;
		allocation_base = top - allocation_size;
		if (!runtime_range_contains(smram_base, smram_size,
			allocation_base, allocation_size) ||
		    runtime_ranges_overlap(output, output_size, allocation_base,
			allocation_size) ||
		    runtime_ranges_overlap(runtime, runtime_size, allocation_base,
			allocation_size) ||
		    smm_save_state_native_span(top, allocation_size,
			RUNTIME_VIEW_RESERVED_SIZE,
			&native_span) != CB_SUCCESS)
			return false;
		for (uint32_t prior = 0; prior < cpu; prior++) {
			const uintptr_t prior_top = __atomic_load_n(
				&smm_runtime.save_state_top[prior], __ATOMIC_ACQUIRE);

			if (prior_top < allocation_size ||
			    runtime_ranges_overlap(allocation_base, allocation_size,
				prior_top - allocation_size, allocation_size))
				return false;
		}
	}
	for (uint32_t cpu = runtime_cpus; cpu < CONFIG_MAX_CPUS; cpu++)
		if (__atomic_load_n(&smm_runtime.save_state_top[cpu],
			__ATOMIC_ACQUIRE))
			return false;
	return true;
}

static bool runtime_output_valid(uintptr_t output, size_t output_size,
	size_t output_alignment)
{
	const uintptr_t runtime = (uintptr_t)&smm_runtime;

	return runtime_range_valid(output, output_size, output_alignment) &&
		runtime_range_valid(runtime, sizeof(smm_runtime),
			SMM_RUNTIME_ALIGNMENT) &&
	    !runtime_ranges_overlap(output, output_size, runtime,
		sizeof(smm_runtime)) &&
	    !runtime_ranges_overlap(output, output_size, (uintptr_t)&runtime_view,
		sizeof(runtime_view));
}

static __noinline bool runtime_geometry_snapshot(uintptr_t output,
	size_t output_size,
	struct runtime_geometry_snapshot *snapshot)
{
	struct runtime_geometry_snapshot value;
	const uintptr_t runtime = (uintptr_t)&smm_runtime;

	value.runtime_cpus = __atomic_load_n(&smm_runtime.num_cpus,
		__ATOMIC_ACQUIRE);
	value.allocation_size = __atomic_load_n(&smm_runtime.save_state_size,
		__ATOMIC_ACQUIRE);
	value.smram_base = __atomic_load_n(&smm_runtime.smbase, __ATOMIC_ACQUIRE);
	value.smram_size = __atomic_load_n(&smm_runtime.smm_size, __ATOMIC_ACQUIRE);
	if (!value.runtime_cpus || value.runtime_cpus > CONFIG_MAX_CPUS ||
	    value.runtime_cpus > SMM_INVOCATION_TOPOLOGY_MAX_CPUS ||
	    value.allocation_size <= RUNTIME_VIEW_RESERVED_SIZE ||
	    !runtime_range_contains(value.smram_base, value.smram_size, runtime,
		sizeof(smm_runtime)) ||
	    !runtime_composition_matches() ||
	    !runtime_topology_matches(value.runtime_cpus) ||
	    !runtime_geometry_valid(value.runtime_cpus, value.allocation_size,
		value.smram_base, value.smram_size, output, output_size, runtime,
		sizeof(smm_runtime)))
		return false;
	*snapshot = value;
	return true;
}

static bool runtime_geometry_unchanged(
	const struct runtime_geometry_snapshot *before, uintptr_t output,
	size_t output_size)
{
	struct runtime_geometry_snapshot after;

	return runtime_geometry_snapshot(output, output_size, &after) &&
		before->runtime_cpus == after.runtime_cpus &&
		before->allocation_size == after.allocation_size &&
		before->smram_base == after.smram_base &&
		before->smram_size == after.smram_size;
}

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **view)
{
	const uintptr_t output = (uintptr_t)view;
	struct runtime_geometry_snapshot snapshot;

	if (!runtime_output_valid(output, sizeof(*view), _Alignof(*view)))
		return CB_ERR_ARG;
	if (!runtime_geometry_snapshot(output, sizeof(*view), &snapshot))
		return CB_ERR;
	RUNTIME_VIEW_TEST_HOOK(1);
	if (!runtime_geometry_unchanged(&snapshot, output, sizeof(*view)))
		return CB_ERR;
	*view = &runtime_view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_cpu_count(
	const struct smm_invocation_runtime_view *view, uint32_t *active_cpus)
{
	const uintptr_t output = (uintptr_t)active_cpus;
	struct runtime_geometry_snapshot snapshot;

	if (view != &runtime_view ||
	    !runtime_output_valid(output, sizeof(*active_cpus),
		_Alignof(*active_cpus)))
		return CB_ERR_ARG;
	if (!runtime_geometry_snapshot(output, sizeof(*active_cpus), &snapshot))
		return CB_ERR;
	RUNTIME_VIEW_TEST_HOOK(2);
	if (!runtime_geometry_unchanged(&snapshot, output, sizeof(*active_cpus)))
		return CB_ERR;
	*active_cpus = snapshot.runtime_cpus;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_save_state_span(
	const struct smm_invocation_runtime_view *view, uint32_t cpu,
	struct smm_save_state_span *span)
{
	const uintptr_t output = (uintptr_t)span;
	struct runtime_geometry_snapshot snapshot;
	struct smm_save_state_span value;
	uintptr_t top;

	if (view != &runtime_view ||
	    !runtime_output_valid(output, sizeof(*span), _Alignof(*span)))
		return CB_ERR_ARG;
	if (!runtime_geometry_snapshot(output, sizeof(*span), &snapshot) ||
	    cpu >= snapshot.runtime_cpus)
		return CB_ERR;
	top = __atomic_load_n(&smm_runtime.save_state_top[cpu], __ATOMIC_ACQUIRE);
	if (smm_save_state_native_span(top, snapshot.allocation_size,
		RUNTIME_VIEW_RESERVED_SIZE, &value) != CB_SUCCESS ||
	    !runtime_range_contains(snapshot.smram_base, snapshot.smram_size,
		value.base, value.size))
		return CB_ERR;
	RUNTIME_VIEW_TEST_HOOK(100U + cpu);
	if (!runtime_geometry_unchanged(&snapshot, output, sizeof(*span)) ||
	    top != __atomic_load_n(&smm_runtime.save_state_top[cpu],
		__ATOMIC_ACQUIRE))
		return CB_ERR;
	*span = value;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base,
	size_t size)
{
	struct runtime_geometry_snapshot snapshot;
	const uintptr_t address = (uintptr_t)base;

	if (view != &runtime_view || !runtime_range_valid(address, size, 1U))
		return CB_ERR_ARG;
	if (!runtime_geometry_snapshot((uintptr_t)&snapshot, sizeof(snapshot),
		&snapshot) ||
	    !runtime_range_contains(snapshot.smram_base, snapshot.smram_size,
		address, size))
		return CB_ERR;
	RUNTIME_VIEW_TEST_HOOK(4);
	return runtime_geometry_unchanged(&snapshot, (uintptr_t)&snapshot,
			sizeof(snapshot)) ? CB_SUCCESS : CB_ERR;
}
#endif

#if CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding)
{
	const uintptr_t output = (uintptr_t)binding;
	struct smm_invocation_runtime_binding value;
	struct runtime_geometry_snapshot snapshot;

	if (!runtime_output_valid(output, sizeof(*binding), _Alignof(*binding)))
		return CB_ERR_ARG;
	if (!runtime_geometry_snapshot(output, sizeof(*binding), &snapshot))
		return CB_ERR;
	value = (struct smm_invocation_runtime_binding) {
		.composition = (const void *)&smm_runtime.invocation_composition,
		.instance = (const void *)&smm_runtime.invocation_loader_instance,
		.evidence = (void *)&smm_runtime.invocation_evidence,
		.topology = (const void *)&smm_runtime.invocation_topology,
	};
	RUNTIME_VIEW_TEST_HOOK(3);
	if (!runtime_geometry_unchanged(&snapshot, output, sizeof(*binding)) ||
	    smm_invocation_loader_composition_evidence(value.composition,
		value.evidence) != value.evidence)
		return CB_ERR;
	*binding = value;
	return CB_SUCCESS;
}

#if CONFIG(SMM_INVOCATION_AUXILIARY_CHANNELS)
enum cb_err smm_invocation_runtime_auxiliary_binding_get(uint32_t index,
	struct smm_invocation_runtime_auxiliary_binding *binding)
{
	const uintptr_t output = (uintptr_t)binding;
	struct smm_invocation_runtime_auxiliary_binding value;
	struct runtime_geometry_snapshot snapshot;

	if (index >= SMM_INVOCATION_AUXILIARY_CHANNEL_COUNT ||
	    !runtime_output_valid(output, sizeof(*binding), _Alignof(*binding)))
		return CB_ERR_ARG;
	if (!runtime_geometry_snapshot(output, sizeof(*binding), &snapshot))
		return CB_ERR;
	value = (struct smm_invocation_runtime_auxiliary_binding) {
		.composition = (const void *)&smm_runtime.invocation_composition,
		.instance = (const void *)&smm_runtime.invocation_loader_instance,
		.topology = (const void *)&smm_runtime.invocation_topology,
		.primary_evidence = (void *)&smm_runtime.invocation_evidence,
		.index = index,
	};
	value.auxiliary_evidence = smm_invocation_auxiliary_channel_evidence(
		&smm_runtime.invocation_auxiliary, value.composition, value.topology,
		value.instance, value.primary_evidence, index);
	if (!value.auxiliary_evidence ||
	    !runtime_geometry_unchanged(&snapshot, output, sizeof(*binding)) ||
	    smm_invocation_auxiliary_channel_evidence(
		&smm_runtime.invocation_auxiliary, value.composition, value.topology,
		value.instance, value.primary_evidence, index) !=
		value.auxiliary_evidence ||
	    !runtime_geometry_unchanged(&snapshot, output, sizeof(*binding)))
		return CB_ERR;
	*binding = value;
	return CB_SUCCESS;
}
#endif
#endif

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

void smm_get_payload_spi_console_buffer(uintptr_t *base, size_t *size)
{
	*base = smm_runtime.payload_spi_console_buffer_base;
	*size = smm_runtime.payload_spi_console_buffer_size;
}

#if CONFIG(STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION)
bool smm_get_dma_receipt_frame(uintptr_t *base, size_t *size)
{
	const uintptr_t frame_base = smm_runtime.dma_receipt_frame_base;
	const size_t frame_size = smm_runtime.dma_receipt_frame_size;
	const uintptr_t smram_base = smm_runtime.smbase;
	const size_t smram_size = smm_runtime.smm_size;

	if (!base || !size || !frame_base || !frame_size || !smram_size ||
	    frame_base > UINT32_MAX || frame_size - 1U > UINT32_MAX - frame_base ||
	    smram_base > (uintptr_t)-1 - (smram_size - 1U) ||
	    spans_overlap(frame_base, frame_size, smram_base, smram_size))
		return false;
	*base = frame_base;
	*size = frame_size;
	return frame_base == smm_runtime.dma_receipt_frame_base &&
		frame_size == smm_runtime.dma_receipt_frame_size;
}

static bool receipt_range_valid(const struct smm_dma_receipt_range *range)
{
	return range->base && range->size && range->base <= UINT32_MAX &&
		range->size - 1U <= UINT32_MAX - range->base;
}

bool smm_get_dma_receipt_memory(const struct smm_dma_receipt_memory **memory)
{
	const struct smm_dma_receipt_memory *value =
		(const void *)&smm_runtime.dma_receipt_memory;

	if (!memory || value->revision != SMM_DMA_RECEIPT_MEMORY_REVISION ||
	    value->size != sizeof(*value) || !receipt_range_valid(&value->frame) ||
	    !receipt_range_valid(&value->dma) ||
	    !receipt_range_valid(&value->mirror) ||
	    value->frame.base != smm_runtime.dma_receipt_frame_base ||
	    value->frame.size != smm_runtime.dma_receipt_frame_size ||
	    spans_overlap(value->frame.base, value->frame.size,
		value->dma.base, value->dma.size) ||
	    spans_overlap(value->frame.base, value->frame.size,
		value->mirror.base, value->mirror.size) ||
	    spans_overlap(value->dma.base, value->dma.size,
		value->mirror.base, value->mirror.size) ||
	    spans_overlap(value->frame.base, value->frame.size,
		smm_runtime.smbase, smm_runtime.smm_size) ||
	    spans_overlap(value->dma.base, value->dma.size,
		smm_runtime.smbase, smm_runtime.smm_size) ||
	    spans_overlap(value->mirror.base, value->mirror.size,
		smm_runtime.smbase, smm_runtime.smm_size))
		return false;
	*memory = value;
	return value->revision == SMM_DMA_RECEIPT_MEMORY_REVISION &&
		value->size == sizeof(*value);
}
#endif

#if CONFIG(CAPSULE_BROKER_FIXED_BUFFERS)
void smm_get_capsule_broker_buffers(
	struct capsule_broker_buffer_reservation *reservation)
{
	*reservation = (struct capsule_broker_buffer_reservation) {
		.communication_base = smm_runtime.capsule_communication_base,
		.communication_reserved_size =
			smm_runtime.capsule_communication_reserved_size,
		.communication_size = smm_runtime.capsule_communication_size,
		.staging_base = smm_runtime.capsule_staging_base,
		.staging_size = smm_runtime.capsule_staging_size,
	};
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

#if CONFIG(PAYLOAD_MM_AUTHVAR_SMM_BOOTSTRAP)
bool smm_take_payload_mm_authvar_arena_receipt(
	struct payload_mm_authvar_smm_arena_receipt *receipt)
{
	return payload_mm_authvar_smm_arena_slot_take(
		(volatile struct payload_mm_authvar_smm_arena_slot *)
		&smm_runtime.authvar_arena, receipt);
}

bool smm_payload_mm_authvar_arena_receipt_consumed(void)
{
	return payload_mm_authvar_smm_arena_slot_consumed(
		&smm_runtime.authvar_arena);
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI)
struct payload_mm_authvar_mor_private_smi_slot *
	smm_get_payload_mm_authvar_mor_private_smi_slot(void)
{
	return (struct payload_mm_authvar_mor_private_smi_slot *)
		&smm_runtime.authvar_mor_channel;
}
#endif

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
#if CONFIG(STM)
	struct smm_save_state_span span;

	if (cpu < 0 || (unsigned int)cpu >= smm_runtime.num_cpus ||
	    smm_save_state_native_span(smm_runtime.save_state_top[cpu],
		smm_runtime.save_state_size, STM_PSD_SIZE, &span) != CB_SUCCESS)
		return NULL;
	return (void *)span.base;
#else
	if (cpu >= smm_runtime.num_cpus)
		return NULL;

	return (void *)(smm_runtime.save_state_top[cpu] -
			(smm_runtime.save_state_size - STM_PSD_SIZE));
#endif
}

uint32_t smm_revision(void)
{
#if CONFIG(STM)
	struct smm_save_state_span span;
	uint32_t revision;

	if (smm_save_state_native_span(smm_runtime.save_state_top[0],
		smm_runtime.save_state_size, STM_PSD_SIZE, &span) != CB_SUCCESS ||
	    smm_save_state_revision_at(&span, SMM_REVISION_OFFSET_FROM_TOP,
		&revision) != CB_SUCCESS)
		return SMM_REV_INVALID;
	return revision;
#else
	const uintptr_t save_state = (uintptr_t)(smm_get_save_state(0));

	return *(uint32_t *)(save_state + smm_runtime.save_state_size
			     - SMM_REVISION_OFFSET_FROM_TOP);
#endif
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
	bool private_smi_handled = false;

	p = arg;
	cpu = p->cpu;
	expected_canary = (uintptr_t)p->canary;

	/* Make sure to set the global runtime. It's OK to race as the value
	 * will be the same across CPUs as well as multiple SMIs. */
	gnvs = (void *)(uintptr_t)smm_runtime.gnvs_ptr;

	if (cpu >= CONFIG_MAX_CPUS) {
		/* Do not log messages to console here, it is not thread safe */
		return;
	}

#if CONFIG(SMM_PRE_LOCK_DISPATCH)
	const enum smm_pre_lock_dispatch_result pre_lock_result =
		smm_pre_lock_dispatch(cpu, p->initial_apic_id);

	switch (pre_lock_result) {
	case SMM_PRE_LOCK_DISPATCH_NOT_HANDLED:
		break;
	case SMM_PRE_LOCK_DISPATCH_PARTICIPANT_HANDLED:
	case SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED:
		actual_canary = *p->canary;
		if (actual_canary != expected_canary)
			smm_invocation_platform_fail_stop();
		if (pre_lock_result == SMM_PRE_LOCK_DISPATCH_BSP_EOS_CONSUMED)
			southbridge_smi_set_eos();
		return;
	default:
		smm_invocation_platform_fail_stop();
	}
#endif

	/* Are we ok to execute the handler? */
	if (!smi_obtain_lock()) {
		/* For security reasons we don't release the other CPUs
		 * until the CPU with the lock is actually done */
		while (smi_handler_status == SMI_LOCKED) {
			asm volatile (
				".byte 0xf3, 0x90\n" /* PAUSE */
			);
		}
		return;
	}

	smi_backup_pci_address();

	smm_soc_early_init();

	console_init();

	printk(BIOS_SPEW, "\nSMI# #%d\n", cpu);

	if (CONFIG(DEBUG_SMI) && CONFIG(CONSOLE_SERIAL))
		exception_init();

	/* Allow drivers to initialize variables in SMM context. */
	if (do_driver_init) {
#if CONFIG(SPI_FLASH_SMM)
		spi_init();
#endif
		do_driver_init = 0;
	}

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_PRIVATE_SMI)
	private_smi_handled = payload_mm_authvar_mor_private_smi_dispatch(cpu);
#endif

	if (!private_smi_handled) {
		cpu_smi_handler();
		northbridge_smi_handler();
		southbridge_smi_handler();
	}

	smi_restore_pci_address();

	actual_canary = *p->canary;

	if (actual_canary != expected_canary) {
#if CONFIG(SMM_INVOCATION_STACK_CANARY_FAIL_STOP)
		smm_invocation_platform_fail_stop();
#else
		printk(BIOS_DEBUG, "canary 0x%lx != 0x%lx\n", actual_canary,
		       expected_canary);

		// Don't die if we can't indicate an error.
		if (CONFIG(DEBUG_SMI))
			die("SMM Handler caused a stack overflow\n");
#endif
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
void __weak mainboard_smi_sleep_finalize(u8 slp_typ) {}
void __weak mainboard_smi_finalize(void) {}

void __weak smm_soc_early_init(void) {}
void __weak smm_soc_exit(void) {}
