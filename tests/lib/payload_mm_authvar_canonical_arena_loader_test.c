/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#include <boot/payload_mm_authvar_smm_loader.h>
#include <boot/payload_mm_authvar_mor_private_smi.h>
#include <commonlib/helpers.h>
#include <commonlib/region.h>
#include <cpu/x86/smm_invocation_loader_composition.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)
#define SMM_CODE_SEGMENT_SIZE 0x10000
#define SMM_REGIONS_ARRAY_SIZE (1 + 1 + CONFIG_MAX_CPUS * 2 + 1 + 1 + 1)

/* Mock only rmodule/fanout/transport boundaries; the extracted loader is unchanged. */
struct rmodule { int unused; };
struct smm_loader_params {
	unsigned int num_cpus, num_concurrent_save_states;
	size_t cpu_save_state_size;
	uintptr_t cr3, handler;
};
struct smm_runtime {
	unsigned int num_cpus;
	struct payload_mm_authvar_smm_arena_slot authvar_arena;
	struct payload_mm_authvar_mor_private_smi_slot authvar_mor_channel;
	struct smm_invocation_topology invocation_topology;
	struct smm_invocation_loader_instance invocation_loader_instance;
	struct smm_invocation_evidence invocation_evidence;
	struct smm_invocation_loader_composition invocation_composition;
	struct payload_mm_authvar_presence_bootstrap authvar_presence_bootstrap;
};
static struct smm_runtime runtime;
static struct { struct region ss, stub_code; } cpus[CONFIG_MAX_CPUS];
static unsigned char _binary_smm_start;
static unsigned int allocations, seeds, required_reads, retires, aborts;
static bool required = true, mor_required;
static unsigned int fault;
static jmp_buf terminal;

void mock_assert(int result, const char *expression, const char *file, int line)
{
	(void)expression; (void)file; (void)line;
	assert(result);
}
void smm_invocation_loader_instance_test_hook(uint32_t point) { (void)point; }
void smm_invocation_topology_test_hook(uint32_t point) { (void)point; }

int printk(int level, const char *format, ...)
{
	(void)level; (void)format;
	return 0;
}
static void print_region(const char *name, struct region region)
{
	(void)name; (void)region;
}
static void scrub_authvar_loader(void *buffer, size_t size)
{
	memset(buffer, 0, size);
}
void __attribute__((noreturn)) die(const char *message, ...)
{
	(void)message;
	longjmp(terminal, 1);
}
static int rmodule_parse(void *image, struct rmodule *module)
{
	(void)image; (void)module; return 0;
}
static size_t rmodule_memory_size(const struct rmodule *module)
{
	(void)module; return 4096;
}
static size_t rmodule_load_alignment(const struct rmodule *module)
{
	(void)module; return 4096;
}
static int rmodule_load(void *base, struct rmodule *module)
{
	(void)base; (void)module; return 0;
}
static struct smm_runtime *rmodule_parameters(const struct rmodule *module)
{
	(void)module; return &runtime;
}
static uintptr_t rmodule_entry(const struct rmodule *module)
{
	(void)module; return 0;
}
static uintptr_t install_page_table(uintptr_t base) { return base; }
static bool smm_create_map(uintptr_t base, unsigned int count,
	struct smm_loader_params *params)
{
	(void)params;
	for (unsigned int index = 0; index < count; index++) {
		cpus[index].ss = region_create(base + index * 8192U, 4096);
		cpus[index].stub_code = region_create(base + index * 8192U + 4096U, 4096);
	}
	return true;
}
static void setup_smihandler_params(struct smm_runtime *handler,
	const struct smm_loader_params *params)
{
	handler->num_cpus = params->num_cpus;
}
static int smm_module_setup_stub(uintptr_t base, size_t size,
	struct smm_loader_params *params, struct smm_invocation_topology *topology,
	unsigned int count)
{
	(void)base; (void)size; (void)params;
	topology->state = SMM_INVOCATION_TOPOLOGY_READY;
	topology->revision = SMM_INVOCATION_TOPOLOGY_REVISION;
	topology->size = sizeof(*topology);
	topology->active_cpus = count;
	for (unsigned int index = 0; index < count; index++)
		topology->initial_apic_ids[index] = index + 8;
	return 0;
}
bool platform_payload_mm_authvar_smm_arena_required(void) { return mor_required; }
bool platform_payload_mm_authvar_mor_private_smi_required(void) { return mor_required; }
void platform_payload_mm_authvar_smm_arena_abort(void) { aborts++; }
bool platform_payload_mm_authvar_smm_arena_seed(
	struct payload_mm_authvar_smm_arena_seed *seed)
{
	seeds++;
	seed->revision = PAYLOAD_MM_AUTHVAR_SMM_ARENA_REVISION;
	seed->size = sizeof(*seed); seed->cold_boot_generation = 99;
	memset(seed->owner, 0x44, sizeof(seed->owner));
	return true;
}
enum cb_err payload_mm_authvar_presence_publication_loader_required(bool *value)
{
	required_reads++; *value = required;
	return fault == 1 ? CB_ERR : CB_SUCCESS;
}
void payload_mm_authvar_presence_tuple_sender_close(void) { retires++; }
void payload_mm_authvar_presence_producer_abort(void) { aborts++; }
void bootmem_reservation_receipt_close(struct bootmem_reservation_receipt_authority *owner)
{
	memset(owner, 0, sizeof(*owner));
}
bool mainboard_authvar_presence_cold_boot(void) { return fault != 6; }
enum cb_err payload_mm_authvar_mor_private_smi_loader_provision(
	struct payload_mm_authvar_mor_private_smi_slot *slot, bool needed)
{
	if (needed) slot->state = 1;
	return CB_SUCCESS;
}
enum cb_err smm_invocation_loader_compose(
	struct smm_invocation_loader_composition *composition,
	struct smm_invocation_topology *topology,
	struct smm_invocation_loader_instance *instance,
	struct smm_invocation_evidence *evidence)
{
	(void)composition; (void)topology; (void)evidence;
	instance->state = SMM_INVOCATION_LOADER_INSTANCE_READY;
	instance->revision = SMM_INVOCATION_LOADER_INSTANCE_REVISION;
	instance->size = sizeof(*instance);
	instance->lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	instance->loader_instance_nonce.low = 17;
	instance->loader_instance_nonce.high = 19;
	return CB_SUCCESS;
}
enum cb_err payload_mm_authvar_presence_tuple_sender_loader_provision(
	struct payload_mm_authvar_presence_bootstrap *slot,
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_topology *topology, bool needed)
{
	assert(needed == required);
	if (!needed) return CB_SUCCESS;
	if (fault == 2) return CB_ERR;
	slot->state = PAYLOAD_MM_AUTHVAR_PRESENCE_BOOTSTRAP_PROVISIONED;
	slot->cold_boot_proven = 1;
	slot->loader_lifecycle = instance->lifecycle;
	slot->loader_nonce = instance->loader_instance_nonce;
	slot->binding = (struct payload_mm_authvar_presence_transaction_binding) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
		.size = sizeof(slot->binding), .generation = 23,
		.transaction_id = 29, .nonce = 31, .maximum_cpus = topology->active_cpus,
	};
	memset(slot->binding.capability, 0x55, sizeof(slot->binding.capability));
	if (fault == 3) runtime.authvar_arena.state = PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY;
	if (fault == 4) slot->cold_boot_proven = 0;
	if (fault == 11) runtime.authvar_arena.receipt.owner[0] = 1;
	return CB_SUCCESS;
}
enum cb_err real_arena_reserve(struct payload_mm_authvar_smm_arena_receipt *receipt,
	uint64_t base, uint64_t size, const struct payload_mm_authvar_range *occupied,
	size_t count, const struct payload_mm_authvar_smm_arena_seed *seed);
enum cb_err payload_mm_authvar_smm_arena_reserve(
	struct payload_mm_authvar_smm_arena_receipt *receipt,
	uint64_t base, uint64_t size, const struct payload_mm_authvar_range *occupied,
	size_t count, const struct payload_mm_authvar_smm_arena_seed *seed)
{
	enum cb_err result;
	allocations++;
	if (fault == 5) return CB_ERR;
	result = real_arena_reserve(receipt, base, size, occupied, count, seed);
	if (fault == 7) runtime.invocation_loader_instance.loader_instance_nonce.high++;
	if (fault == 8) runtime.invocation_topology.initial_apic_ids[1]++;
	if (fault == 9) runtime.authvar_presence_bootstrap.binding.capability[0]++;
	if (fault == 10) runtime.authvar_arena.state = PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY;
	return result;
}
void payload_mm_authvar_smm_loader_scrub_test_hook(const void *buffer, size_t size)
{
	(void)buffer; (void)size;
}

#include "actual-region.c"
#include "actual-arena-loader.c"

int main(int argc, char **argv)
{
	struct smm_loader_params params = {
		.num_cpus = 2, .num_concurrent_save_states = 2, .cpu_save_state_size = 4096,
	};
	assert(argc == 4);
	required = atol(argv[1]) != 0;
	mor_required = atol(argv[2]) != 0;
	fault = (unsigned int)atol(argv[3]);
	if (setjmp(terminal)) {
		assert(required && fault >= 2);
		if (fault != 2) {
			assert(retires == 1 && !runtime.authvar_arena.state &&
				!runtime.authvar_mor_channel.state);
		}
		return 0;
	}
	int result = smm_load_module(0x10000000, 0x1000000, &params);
	assert(required_reads == 1);
	if (fault == 1) {
		assert(result == -1 && !allocations && retires == 1);
		return 0;
	}
	assert(!fault && result == 0);
	const bool canonical = required && CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED);
	assert(allocations == (canonical || mor_required ? 1U : 0U));
	assert(seeds == (!canonical && mor_required ? 1U : 0U));
	if (canonical) {
		assert(runtime.authvar_arena.state == PAYLOAD_MM_AUTHVAR_SMM_ARENA_READY);
		assert(runtime.authvar_arena.receipt.cold_boot_generation == 23);
		assert(!memcmp(runtime.authvar_arena.receipt.owner,
			runtime.authvar_presence_bootstrap.binding.capability,
			PAYLOAD_MM_AUTHVAR_SMM_ARENA_OWNER_SIZE));
	}
	return 0;
}
