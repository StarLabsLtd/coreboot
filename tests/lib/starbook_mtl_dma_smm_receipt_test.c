/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <cpu/x86/smm.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <cpu/x86/smm_invocation_loader_instance.h>
#include <cpu/x86/smm_invocation_topology.h>

#include "../../src/lib/payload_mm_crypto/crypto.h"
#include "../../src/mainboard/starlabs/starbook/variants/mtl/dma_smm_receipt_provision.h"

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static struct starbook_mtl_dma_receipt_frame frame __aligned(8);
static struct smm_invocation_loader_composition composition;
static struct smm_invocation_loader_instance instance;
static struct smm_invocation_evidence evidence;
static struct smm_invocation_topology topology;
static struct smm_invocation_save_state_ops ops;
static struct smm_invocation_runtime_view *view = (void *)0x1234;
static uint64_t wire;
static bool frame_is_protected;
static bool mutate_frame;
static bool wrong_range;
static bool allow_dram = true;
static unsigned int hash_calls;
static unsigned int verify_calls;
static uint64_t smram_base = 0x800000;
static uint8_t dependency_context;
static struct starbook_mtl_dma_receipt_dependencies dependencies;
static const void *unprotected;
static bool mutate_dependencies;
static uint64_t rejected_base = UINT64_MAX;
static struct starbook_mtl_dma_receipt_frame *pinned_frame = &frame;

bool starbook_mtl_dma_receipt_layout_matches_memory_test(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	const struct smm_dma_receipt_memory *memory);

static void test_pinned_layout(void)
{
	struct smm_dma_receipt_memory memory = {
		.revision = SMM_DMA_RECEIPT_MEMORY_REVISION,
		.size = sizeof(memory),
		.dma = { 0x100000, 0x10000 },
		.mirror = { 0x300000, 0x4000 },
	};
	struct starbook_mtl_dma_smm_receipt receipt = {
		.handoff = { 0x100000, 0x1000 },
		.tables = { 0x101000, 0x4000 },
		.table_mirror = { 0x300000, 0x4000 },
		.arenas = {
			{ 0x105000, 0x3000 },
			{ 0x108000, 0x4000 },
			{ 0x10c000, 0x4000 },
		},
	};
	struct starbook_mtl_dma_smm_receipt mutated;

	assert(starbook_mtl_dma_receipt_layout_matches_memory_test(&receipt,
		&memory));
#define REJECT_MUTATION(member, value) do { \
	mutated = receipt; \
	mutated.member = (value); \
	assert(!starbook_mtl_dma_receipt_layout_matches_memory_test(&mutated, \
		&memory)); \
} while (0)
	REJECT_MUTATION(handoff.base, receipt.handoff.base + 0x1000);
	REJECT_MUTATION(handoff.size, 0x2000);
	REJECT_MUTATION(tables.base, receipt.tables.base + 0x1000);
	REJECT_MUTATION(tables.size, receipt.tables.size + 0x1000);
	REJECT_MUTATION(table_mirror.base, receipt.table_mirror.base + 0x1000);
	REJECT_MUTATION(table_mirror.size, receipt.table_mirror.size + 0x1000);
	REJECT_MUTATION(arenas[0].base, receipt.arenas[0].base + 0x1000);
	REJECT_MUTATION(arenas[0].size, receipt.arenas[0].size + 0x1000);
	REJECT_MUTATION(arenas[1].base, receipt.arenas[1].base + 0x1000);
	REJECT_MUTATION(arenas[1].size, receipt.arenas[1].size + 0x1000);
	REJECT_MUTATION(arenas[2].base, receipt.arenas[2].base + 0x1000);
	REJECT_MUTATION(arenas[2].size, receipt.arenas[2].size - 0x1000);
#undef REJECT_MUTATION
}

static enum smm_invocation_match match_apmc(void *context, uint32_t cpu,
	uint8_t command)
{
	(void)context;
	return cpu == 0 && command == SMM_APMC_STARBOOK_MTL_DMA_RECEIPT ?
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
	*base = (uintptr_t)pinned_frame + wrong_range;
	*size = sizeof(frame);
	return true;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = smram_base;
	*size = 0x100000;
}

static bool ordinary_dram_range(void *context, uint64_t base, size_t size)
{
	assert(context == &dependency_context);
	if (mutate_dependencies)
		dependencies.size++;
	if (!allow_dram || base == rejected_base)
		return false;
	if (base == (uintptr_t)pinned_frame)
		return size == sizeof(*pinned_frame);
	return (base == 0x100000 && size == 0x1000) ||
		(base == 0x200000 && size == 0x4000) ||
		(base == 0x300000 && size == 0x4000) ||
		(base == 0x400000 && size == 0x1000) ||
		(base == 0x500000 && size == 0x1000) ||
		(base == 0x600000 && size == 0x1000);
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
	*output = view;
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

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *runtime_view, const void *base,
	size_t size)
{
	assert(runtime_view == view && base && size);
	if (base == &frame)
		return frame_is_protected ? CB_SUCCESS : CB_ERR;
	if (base == unprotected)
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
	hash_calls++;
	assert(message == (void *)(uintptr_t)frame.candidate.tables.base);
	assert(message_size == frame.candidate.tables.size);
	memset(digest, 0x5a, 32);
	return PAYLOAD_MM_VERIFY_OK;
}

bool starbook_mtl_dma_smm_receipt_geometry_valid(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t forbidden_base, size_t forbidden_size)
{
	const uint64_t base = receipt->tables.base;
	const uint64_t size = receipt->tables.size;

	assert(forbidden_base == smram_base && forbidden_size == 0x100000);
	return base && size && base <= UINT32_MAX &&
		size - 1U <= UINT32_MAX - base &&
		!(base >= 0xc0000000 && base < 0xd0000000) &&
		!(base < forbidden_base + forbidden_size &&
		  forbidden_base < base + size);
}

enum cb_err starbook_mtl_dma_smm_verify(
	const struct starbook_mtl_dma_smm_receipt *receipt,
	uint64_t lifecycle_base, size_t lifecycle_size,
	const struct starbook_mtl_dma_smm_observer *observer,
	const struct smm_invocation_runtime_view *runtime_view,
	struct starbook_mtl_dma_smm_workspace *workspace)
{
	verify_calls++;
	assert(receipt && lifecycle_base && lifecycle_size && observer && workspace);
	assert(runtime_view == view);
	assert(receipt->loader_instance_nonce.low == 1);
	assert(receipt->invocation_generation == evidence.generation);
	assert(receipt->loader_lifecycle == instance.lifecycle);
	for (size_t index = 0; index < sizeof(receipt->table_digest); index++)
		assert(receipt->table_digest[index] == 0x5a);
	if (mutate_frame)
		frame.candidate.revision++;
	return CB_SUCCESS;
}

static void initialize(void)
{
	frame = (struct starbook_mtl_dma_receipt_frame) {
		.revision = STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION,
		.size = sizeof(frame),
		.state = STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST,
		.candidate = {
			.revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION,
			.size = sizeof(frame.candidate),
			.handoff = { 0x100000, 0x1000 },
			.tables = { 0x200000, 0x4000 },
			.table_mirror = { 0x300000, 0x4000 },
			.arenas = {
				{ 0x400000, 0x1000 },
				{ 0x500000, 0x1000 },
				{ 0x600000, 0x1000 },
			},
		},
	};
	topology.active_cpus = 4;
	topology.bsp_cpu = 0;
	instance.loader_instance_nonce.low = 1;
	instance.lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	evidence.loader_instance_nonce = instance.loader_instance_nonce;
	evidence.loader_lifecycle = instance.lifecycle;
	evidence.generation = 7;
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
	struct starbook_mtl_dma_smm_binding binding;

	assert(argc == 2);
	test_pinned_layout();
	initialize();
	if (!strcmp(argv[1], "wrong-range"))
		wrong_range = true;
	else if (!strcmp(argv[1], "protected-frame"))
		frame_is_protected = true;
	else if (!strcmp(argv[1], "mutated-frame"))
		mutate_frame = true;
	else if (!strcmp(argv[1], "wrong-state"))
		frame.state = STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED;
	else if (!strcmp(argv[1], "null-table"))
		frame.candidate.tables.base = 0;
	else if (!strcmp(argv[1], "above-4g"))
		frame.candidate.tables.base = 0x100000000ULL;
	else if (!strcmp(argv[1], "huge-table"))
		frame.candidate.tables.size = UINT64_MAX;
	else if (!strcmp(argv[1], "mmio-table"))
		frame.candidate.tables.base = 0xc0000000;
	else if (!strcmp(argv[1], "smram-table"))
		frame.candidate.tables.base = smram_base;
	else if (!strcmp(argv[1], "unallowed-dram"))
		allow_dram = false;
	else if (!strcmp(argv[1], "unallowed-frame")) {
		pinned_frame = mmap(NULL, sizeof(*pinned_frame), PROT_NONE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
		assert(pinned_frame != MAP_FAILED &&
			(uintptr_t)pinned_frame <= UINT32_MAX);
		rejected_base = (uintptr_t)pinned_frame;
		wire = (uint64_t)(uint32_t)(uintptr_t)pinned_frame << 32 |
			STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST;
	} else if (!strcmp(argv[1], "unallowed-handoff"))
		rejected_base = frame.candidate.handoff.base;
	else if (!strcmp(argv[1], "unallowed-tables"))
		rejected_base = frame.candidate.tables.base;
	else if (!strcmp(argv[1], "unallowed-mirror"))
		rejected_base = frame.candidate.table_mirror.base;
	else if (!strcmp(argv[1], "unallowed-arena0"))
		rejected_base = frame.candidate.arenas[0].base;
	else if (!strcmp(argv[1], "unallowed-arena1"))
		rejected_base = frame.candidate.arenas[1].base;
	else if (!strcmp(argv[1], "unallowed-arena2"))
		rejected_base = frame.candidate.arenas[2].base;
	else if (!strcmp(argv[1], "unprotected-dependencies"))
		unprotected = &dependencies;
	else if (!strcmp(argv[1], "unprotected-dependency-context"))
		unprotected = &dependency_context;
	else if (!strcmp(argv[1], "unprotected-dependency-callback"))
		unprotected = (const void *)(uintptr_t)ordinary_dram_range;
	else if (!strcmp(argv[1], "dependency-toctou"))
		mutate_dependencies = true;
	else if (strcmp(argv[1], "valid") && strcmp(argv[1], "generation"))
		return 2;

	if (!strcmp(argv[1], "valid") || !strcmp(argv[1], "generation")) {
		assert(starbook_mtl_dma_receipt_provision_receive(&ops) == CB_SUCCESS);
		assert(frame.state == STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED);
		assert(wire == STARBOOK_MTL_DMA_RECEIPT_WIRE_SUCCESS);
		if (!strcmp(argv[1], "generation")) {
			evidence.generation++;
			assert(starbook_mtl_dma_smm_binding_get(&binding) != CB_SUCCESS);
		} else {
			assert(starbook_mtl_dma_smm_binding_get(&binding) == CB_SUCCESS);
			assert(binding.receipt->invocation_generation == 7);
		}
	} else {
		assert(starbook_mtl_dma_receipt_provision_receive(&ops) != CB_SUCCESS);
		if (strcmp(argv[1], "mutated-frame"))
			assert(hash_calls == 0);
		if (strcmp(argv[1], "mutated-frame"))
			assert(verify_calls == 0);
	}
	return 0;
}
