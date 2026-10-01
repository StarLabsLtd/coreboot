/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_fail_stop.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <cpu/x86/smm_invocation_runtime.h>
#include <cpu/x86/smm_invocation_topology.h>
#include <soc/intel/common/block/vtd/vtd_translation.h>

#include "../../src/lib/payload_mm_crypto/crypto.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

#define ECAM_BASE 0xc0000000UL
#define ECAM_BUSES 2U
#define GFX_BASE 0xfc800000UL
#define VTVC0_BASE 0xfc801000UL
#define GFXVTBAR_REGISTER 0xfedc5410UL
#define TABLE_BASE 0x200000UL
#define MIRROR_BASE 0x300000UL
#define TABLE_PAGES 32U
#define PAGE_SIZE 4096U

static struct starbook_mtl_dma_receipt_frame frame __aligned(8);
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct smm_invocation_save_state_ops ops;
static struct smm_invocation_runtime_view *runtime_view = (void *)0x1234;
static struct starbook_mtl_dma_receipt_dependencies dependencies;
static uint8_t dependency_context;
static uint64_t wire;

static void map_fixed(uintptr_t base, size_t size)
{
	void *mapped = mmap((void *)base, size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

	assert(mapped == (void *)base);
}

static volatile uint32_t *config32(uint8_t bus, uint8_t devfn,
	uint16_t offset)
{
	return (void *)(ECAM_BASE + ((uintptr_t)bus << 20) +
		((uintptr_t)devfn << 12) + offset);
}

static void add_function(uint8_t bus, uint8_t devfn, uint16_t vendor,
	uint16_t device, uint32_t class, uint8_t header)
{
	*config32(bus, devfn, 0) = (uint32_t)device << 16 | vendor;
	*config32(bus, devfn, 4) = 3U;
	*config32(bus, devfn, 8) = class << 8;
	*config32(bus, devfn, 0xc) = (uint32_t)header << 16;
}

static void initialize_hardware(void)
{
	map_fixed(ECAM_BASE, (size_t)ECAM_BUSES << 20);
	map_fixed(GFX_BASE, 2U * PAGE_SIZE);
	map_fixed(GFXVTBAR_REGISTER & ~(uintptr_t)(PAGE_SIZE - 1U), PAGE_SIZE);
	map_fixed(TABLE_BASE, TABLE_PAGES * PAGE_SIZE);
	map_fixed(MIRROR_BASE, TABLE_PAGES * PAGE_SIZE);
	for (uint16_t bus = 0; bus < ECAM_BUSES; bus++)
		for (uint16_t devfn = 0; devfn < 256U; devfn++)
			*config32((uint8_t)bus, (uint8_t)devfn, 0) = UINT32_MAX;
	add_function(0, 0x10, 0x8086, 0x1111, 0x030000, 0);
	add_function(0, 0x28, 0x8086, 0x2222, 0x048000, 0);
	add_function(0, 0x31, 0x8086, 0x3333, 0x060400, 1);
	*config32(0, 0x31, 0x18) = 0x00010100U;
	add_function(0, 0x68, 0x8086, 0x4444, 0x0c0330, 0);
	add_function(0, 0xa0, 0x8086, 0x5555, 0x0c0330, 0);
	add_function(1, 0, 0x1234, 0x6666, 0x010802, 0);
	*(volatile uint32_t *)(VTVC0_BASE + 0x1c) =
		(1U << 30) | (1U << 31);
	*(volatile uint64_t *)(VTVC0_BASE + 0x20) = TABLE_BASE;
	*(volatile uint32_t *)(VTVC0_BASE + 0x64) = 0;
	*(volatile uint32_t *)(GFX_BASE + 0x1c) = 0;
	*(volatile uint32_t *)(GFX_BASE + 0x64) = 0;
	*(volatile uint64_t *)GFXVTBAR_REGISTER = GFX_BASE | 1U;
}

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	return !cpu && command == SMM_APMC_STARBOOK_MTL_DMA_RECEIPT ?
		SMM_INVOCATION_MATCHED : SMM_INVOCATION_NOT_MATCHED;
}

static enum cb_err read_value(void *context, uint32_t cpu, uint64_t *value)
{
	(void)context;
	if (cpu)
		return CB_ERR;
	*value = wire;
	return CB_SUCCESS;
}

static enum cb_err write_value(void *context, uint32_t cpu, uint64_t value)
{
	(void)context;
	if (cpu)
		return CB_ERR;
	wire = value;
	return CB_SUCCESS;
}

bool smm_get_dma_receipt_frame(uintptr_t *base, size_t *size)
{
	*base = (uintptr_t)&frame;
	*size = sizeof(frame);
	return true;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = 0x90000000U;
	*size = 0x100000U;
}

static bool ordinary_dram_range(void *context, uint64_t base, size_t size)
{
	(void)context;
	return (base == (uintptr_t)&frame && size == sizeof(frame)) ||
		(base == frame.candidate.handoff.base &&
		 size == frame.candidate.handoff.size) ||
		(base == frame.candidate.tables.base &&
		 size == frame.candidate.tables.size) ||
		(base == frame.candidate.table_mirror.base &&
		 size == frame.candidate.table_mirror.size) ||
		(base == frame.candidate.arenas[0].base &&
		 size == frame.candidate.arenas[0].size) ||
		(base == frame.candidate.arenas[1].base &&
		 size == frame.candidate.arenas[1].size) ||
		(base == frame.candidate.arenas[2].base &&
		 size == frame.candidate.arenas[2].size);
}

enum cb_err starbook_mtl_dma_receipt_policy(
	const struct starbook_mtl_dma_receipt_dependencies **output)
{
	*output = &dependencies;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **output)
{
	*output = runtime_view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_binding_get(
	struct smm_invocation_runtime_binding *binding)
{
	*binding = (struct smm_invocation_runtime_binding) {
		.composition = &composition,
		.instance = &instance,
		.evidence = &evidence,
		.topology = &topology,
	};
	return CB_SUCCESS;
}

void smm_invocation_evidence_test_hook(uint32_t point)
{
	(void)point;
}

void smm_invocation_platform_fail_stop(void)
{
	abort();
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *view, const void *base,
	size_t size)
{
	if (view != runtime_view || !base || !size || base == &frame)
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_topology_read(
	const struct smm_invocation_topology *candidate,
	struct smm_invocation_topology *snapshot)
{
	assert(candidate == &topology);
	*snapshot = topology;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_loader_instance_read(
	const struct smm_invocation_loader_instance *candidate,
	struct smm_invocation_loader_instance *snapshot)
{
	assert(candidate == &instance);
	*snapshot = instance;
	return CB_SUCCESS;
}

enum payload_mm_verify_status payload_mm_sha256(const void *message,
	size_t message_size, uint8_t digest[32])
{
	const uint8_t *bytes = message;
	uint32_t hash = 2166136261U;

	for (size_t index = 0; index < message_size; index++)
		hash = (hash ^ bytes[index]) * 16777619U;
	for (size_t index = 0; index < 32U; index++) {
		hash = hash * 1103515245U + 12345U;
		digest[index] = (uint8_t)(hash >> 24);
	}
	return PAYLOAD_MM_VERIFY_OK;
}

static void initialize_receipt(bool forged_graph)
{
	const uint16_t bdfs[] = { forged_graph ? 1U : 0x100U, 0xa0U, 0x68U };
	const struct starbook_mtl_dma_smm_range arenas[] = {
		{ 0x800000, 4U * PAGE_SIZE },
		{ 0x810000, 4U * PAGE_SIZE },
		{ 0x820000, 4U * PAGE_SIZE },
	};
	struct vtd_translation_requester requesters[3];
	struct vtd_translation_image image = {
		.memory = (void *)TABLE_BASE,
		.physical_base = TABLE_BASE,
		.capacity_pages = TABLE_PAGES,
	};

	memset((void *)TABLE_BASE, 0, TABLE_PAGES * PAGE_SIZE);
	for (size_t index = 0; index < 3U; index++)
		requesters[index] = (struct vtd_translation_requester) {
			.bdf = bdfs[index],
			.domain = (uint16_t)(index + 1U),
			.cpu_base = arenas[index].base,
			.device_base = arenas[index].base,
			.pages = (uint32_t)(arenas[index].size / PAGE_SIZE),
		};
	assert(!vtd_translation_build(&image, requesters, 3U));
	memcpy((void *)MIRROR_BASE, (void *)TABLE_BASE, TABLE_PAGES * PAGE_SIZE);
	frame = (struct starbook_mtl_dma_receipt_frame) {
		.revision = STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION,
		.size = sizeof(frame),
		.state = STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST,
		.candidate = {
			.revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION,
			.size = sizeof(frame.candidate),
			.bus_count = ECAM_BUSES,
			.function_count = 6,
			.gfx_mode = STARBOOK_MTL_DMA_SMM_GFX_QUIESCED,
			.ecam_base = ECAM_BASE,
			.vtvc0_base = VTVC0_BASE,
			.vtvc0_rtaddr = TABLE_BASE,
			.gfx_base = GFX_BASE,
			.gfxvtbar_register = GFXVTBAR_REGISTER,
			.gfxvtbar_value = GFX_BASE | 1U,
			.handoff = { 0x100000, PAGE_SIZE },
			.tables = { TABLE_BASE, TABLE_PAGES * PAGE_SIZE },
			.table_mirror = { MIRROR_BASE, TABLE_PAGES * PAGE_SIZE },
			.arenas = { arenas[0], arenas[1], arenas[2] },
			.table_used_bytes = image.used_pages * PAGE_SIZE,
			.functions = {
				{ 0x10, 0x8086, 0x1111, 3, 0x030000 },
				{ 0x28, 0x8086, 0x2222, 3, 0x048000 },
				{ 0x31, 0x8086, 0x3333, 3, 0x060400 },
				{ 0x68, 0x8086, 0x4444, 3, 0x0c0330 },
				{ 0xa0, 0x8086, 0x5555, 3, 0x0c0330 },
				{ 0x100, 0x1234, 0x6666, 3, 0x010802 },
			},
		},
	};
}

static void initialize(bool forged_graph)
{
	initialize_hardware();
	initialize_receipt(forged_graph);
	topology.active_cpus = 4;
	topology.bsp_cpu = 0;
	instance.loader_instance_nonce.low = 1;
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	evidence.loader_instance_nonce = instance.loader_instance_nonce;
	evidence.loader_lifecycle = instance.lifecycle;
	evidence.state = SMM_INVOCATION_READY;
	evidence.generation = 7;
	evidence.closed_generation = 7;
	evidence.closed_loader_instance_nonce = instance.loader_instance_nonce;
	evidence.closed_lifecycle = instance.lifecycle;
	evidence.closed_eos_consumed = 1;
	evidence.active_cpus = topology.active_cpus;
	evidence.bsp_cpu = topology.bsp_cpu;
	evidence.expected_cpus = 0xfU;
	dependencies = (struct starbook_mtl_dma_receipt_dependencies) {
		.revision = STARBOOK_MTL_DMA_RECEIPT_DEPENDENCIES_REVISION,
		.size = sizeof(dependencies),
		.context = &dependency_context,
		.context_size = sizeof(dependency_context),
		.ordinary_dram_range = ordinary_dram_range,
	};
	ops = (struct smm_invocation_save_state_ops) {
		.match_apmc_write = match_apmc,
		.read_value = read_value,
		.write_value = write_value,
	};
	wire = (uint64_t)(uint32_t)(uintptr_t)&frame << 32 |
		STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST;
}

int main(int argc, char **argv)
{
	struct starbook_mtl_dma_smm_binding output = {
		.receipt = (void *)0x12345678,
	};
	const bool forged = argc == 2 && !strcmp(argv[1], "forged-graph");
	struct starbook_mtl_dma_smm_epoch_range ranges[2] = {
		{ 0x700000, PAGE_SIZE }, { 0x710000, PAGE_SIZE },
	};
	struct smm_invocation_entry_ticket ticket = { 0 };

	assert(argc == 2);
	initialize(forged);
	if (!strcmp(argv[1], "valid-s3") || !strcmp(argv[1], "epoch-s3")) {
		instance.loader_instance_nonce.low = 2;
		instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
		evidence.loader_instance_nonce = instance.loader_instance_nonce;
		evidence.closed_loader_instance_nonce = instance.loader_instance_nonce;
		evidence.loader_lifecycle = instance.lifecycle;
		evidence.closed_lifecycle = instance.lifecycle;
	}
	if (forged) {
		assert(starbook_mtl_dma_receipt_provision_receive(&ops) != CB_SUCCESS);
		return 0;
	}
	assert(starbook_mtl_dma_receipt_provision_receive(&ops) == CB_SUCCESS);
	assert(starbook_mtl_dma_smm_binding_get(&output) == CB_SUCCESS);
	assert(output.receipt && output.receipt != (void *)0x12345678);
	if (!strcmp(argv[1], "epoch-s3")) {
		assert(starbook_mtl_dma_smm_epoch_prepare(&instance, &evidence,
			&topology, ranges) == CB_SUCCESS);
		evidence.generation++;
		evidence.closed_generation++;
		ticket = (struct smm_invocation_entry_ticket) {
			.generation = evidence.generation,
			.loader_instance_nonce = instance.loader_instance_nonce,
			.lifecycle = instance.lifecycle,
			.cpu = topology.bsp_cpu,
		};
		assert(starbook_mtl_dma_smm_epoch_activate(&instance, &evidence,
			&topology, &ticket, ranges) == CB_SUCCESS);
		assert(starbook_mtl_dma_smm_epoch_range_protected(NULL,
			ranges[0].base, ranges[0].size));
		return 0;
	}
	output.receipt = (void *)0x12345678;
	if (!strcmp(argv[1], "table-drift"))
		*(volatile uint64_t *)TABLE_BASE ^= 0x1000U;
	else if (!strcmp(argv[1], "ecam-drift"))
		*config32(0, 0xa0, 8) ^= 0x100U;
	else if (!strcmp(argv[1], "stale-s3")) {
		instance.loader_instance_nonce.low++;
		instance.lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
		evidence.loader_instance_nonce = instance.loader_instance_nonce;
		evidence.loader_lifecycle = instance.lifecycle;
	} else if (strcmp(argv[1], "valid") && strcmp(argv[1], "valid-s3")) {
		return 2;
	}
	if (strcmp(argv[1], "valid") && strcmp(argv[1], "valid-s3")) {
		assert(starbook_mtl_dma_smm_binding_get(&output) != CB_SUCCESS);
		assert(output.receipt == (void *)0x12345678);
		assert(starbook_mtl_dma_smm_binding_get(&output) != CB_SUCCESS);
	}
	return 0;
}
