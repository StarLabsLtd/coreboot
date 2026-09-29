/* SPDX-License-Identifier: GPL-2.0-only */

#include "../../src/mainboard/starlabs/starbook/variants/mtl/authvar_presence_lifecycle_close_mailbox.h"

#include <cpu/x86/smm.h>
#include <stdlib.h>
#include <string.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority slot;
static struct smm_invocation_runtime_view *view = (void *)1;
static struct starbook_mtl_dma_smm_receipt dma_receipt;
static struct starbook_mtl_dma_smm_binding dma_binding;
static const void *unprotected;
static size_t unprotected_size;
static bool reject_dma;
static bool reenter_dma;
static bool inside_dma;
static bool nested_result;
static struct starbook_mtl_lifecycle_mailbox_binding *reenter_context;

enum cb_err bootmem_reservation_receipt_mac(const uint8_t key[32],
	const void *message, size_t size, uint8_t mac[32]);

static bool overlaps(const void *first, size_t first_size,
	const void *second, size_t second_size)
{
	const uintptr_t a = (uintptr_t)first;
	const uintptr_t b = (uintptr_t)second;

	return a <= b ? b - a < first_size : a - b < second_size;
}

struct payload_mm_authvar_presence_lifecycle_close_mailbox_authority *
smm_get_payload_mm_authvar_presence_lifecycle_close_mailbox_authority(void)
{
	return &slot;
}

enum cb_err smm_invocation_runtime_view_get(
	const struct smm_invocation_runtime_view **output)
{
	*output = view;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_range_is_protected(
	const struct smm_invocation_runtime_view *runtime_view, const void *base,
	size_t size)
{
	assert(runtime_view == view);
	return unprotected && overlaps(base, size, unprotected, unprotected_size) ?
		CB_ERR : CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_cpu_count(
	const struct smm_invocation_runtime_view *runtime_view, uint32_t *cpus)
{
	assert(runtime_view == view);
	*cpus = 1;
	return CB_SUCCESS;
}

enum cb_err smm_invocation_runtime_save_state_span(
	const struct smm_invocation_runtime_view *runtime_view, uint32_t cpu,
	struct smm_save_state_span *span)
{
	assert(runtime_view == view && cpu == 0);
	*span = (struct smm_save_state_span) { .base = 0x100000, .size = 0x1000 };
	return CB_SUCCESS;
}

void smm_region(uintptr_t *base, size_t *size)
{
	*base = 0x200000;
	*size = 0x10000;
}

enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding)
{
	if (reject_dma)
		return CB_ERR;
	if (reenter_dma && !inside_dma) {
		inside_dma = true;
		nested_result = starbook_mtl_lifecycle_mailbox_dma_protected(
			reenter_context, 0x400000, 0x1000);
		inside_dma = false;
	}
	*binding = dma_binding;
	return CB_SUCCESS;
}

static void prepare(struct bootmem_reservation_receipt *receipt)
{
	struct bootmem_reservation_receipt_authority signer = { 0 };
	struct bootmem_aligned_reservation_handle handle = { .opaque = { 1, 2 } };
	uint8_t secret[BOOTMEM_RESERVATION_RECEIPT_SECRET_SIZE];

	starbook_mtl_lifecycle_mailbox_reset_test();
	memset(&slot, 0, sizeof(slot));
	memset(secret, 0x5a, sizeof(secret));
	assert(bootmem_reservation_receipt_provision(&signer, &slot.verifier,
		secret, BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT, 7, &handle) ==
		CB_SUCCESS);
	slot.revision =
		PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_REVISION;
	slot.size = sizeof(slot);
	slot.loader_instance_nonce.low = 0x11;
	slot.loader_instance_nonce.high = 0x22;
	slot.invocation_generation = 7;
	slot.loader_lifecycle = SMM_INVOCATION_LOADER_NON_S3_LOAD;
	slot.state = PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_MAILBOX_READY;
	dma_receipt = (struct starbook_mtl_dma_smm_receipt) {
		.revision = STARBOOK_MTL_DMA_SMM_RECEIPT_REVISION,
		.size = sizeof(dma_receipt),
		.bus_count = 256,
		.function_count = 1,
		.gfx_mode = STARBOOK_MTL_DMA_SMM_GFX_QUIESCED,
		.ecam_base = 0xc0000000,
		.vtvc0_base = 0xfc801000,
		.vtvc0_rtaddr = 0x500000,
		.gfx_base = 0xfc800000,
		.gfxvtbar_register = 0xfedc5410,
		.gfxvtbar_value = 0xfc800001,
		.handoff = { 0x300000, 0x1000 },
		.tables = { 0x500000, 0x1000 },
		.table_mirror = { 0x600000, 0x1000 },
		.arenas = {
			{ 0x700000, 0x1000 }, { 0x800000, 0x1000 },
			{ 0x900000, 0x1000 },
		},
		.table_used_bytes = 0x1000,
		.loader_instance_nonce = slot.loader_instance_nonce,
		.invocation_generation = 7,
		.loader_lifecycle = slot.loader_lifecycle,
	};
	dma_binding = (struct starbook_mtl_dma_smm_binding) {
		.receipt = &dma_receipt,
		.current = {
			.loader_instance_nonce = slot.loader_instance_nonce,
			.invocation_generation = 9,
			.loader_lifecycle = slot.loader_lifecycle,
		},
	};
	*receipt = (struct bootmem_reservation_receipt) {
		.revision = BOOTMEM_RESERVATION_RECEIPT_REVISION,
		.size = sizeof(*receipt),
		.boot_kind = BOOTMEM_RESERVATION_RECEIPT_COLD_BOOT,
		.generation = 7,
		.sequence = 1,
		.handle = handle,
		.base = 0x400000,
		.bytes = 0x1000,
		.tag = BM_MEM_RESERVED,
		.use = BOOTMEM_RESERVATION_RECEIPT_ACTIVE_FIRMWARE,
	};
	assert(bootmem_reservation_receipt_mac(signer.secret, receipt,
		offsetof(struct bootmem_reservation_receipt, mac), receipt->mac) ==
		CB_SUCCESS);
	bootmem_reservation_receipt_close(&signer);
	unprotected = NULL;
	unprotected_size = 0;
	reject_dma = false;
	reenter_dma = false;
	inside_dma = false;
	nested_result = true;
	reenter_context = NULL;
}

static void success_and_runtime_reproof(void)
{
	struct bootmem_reservation_receipt receipt;
	struct starbook_mtl_lifecycle_mailbox_binding binding = { 0 };
	struct smm_invocation_save_state_ops ops = { 0 };
	uint8_t frame[4096];

	prepare(&receipt);
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) ==
		CB_SUCCESS);
	assert(binding.base == 0x400000 && binding.size == 0x1000 &&
		binding.invocation_generation == 7);
	assert(starbook_mtl_lifecycle_mailbox_dma_context_size() ==
		sizeof(binding));
	assert(starbook_mtl_lifecycle_mailbox_dma_protected(
		&binding, 0x400000,
		0x1000));
	dma_binding.current.invocation_generation = 10;
	assert(starbook_mtl_lifecycle_mailbox_dma_protected(
		&binding, 0x400000,
		0x1000));
	assert(!starbook_mtl_lifecycle_mailbox_dma_protected(
		&binding, 0x401000,
		0x1000));
}

static void rejects_hostile_inputs(void)
{
	struct bootmem_reservation_receipt receipt;
	struct starbook_mtl_lifecycle_mailbox_binding binding = { 0 };
	struct smm_invocation_save_state_ops ops = { 0 };
	uint8_t frame[4096];

	prepare(&receipt);
	receipt.tag = BM_MEM_TABLE;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	receipt.mac[0] ^= 1U;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	receipt.handle.opaque[0]++;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	receipt.use++;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	receipt.generation++;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	receipt.base += 0x1000;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	receipt.bytes += 0x1000;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	slot.loader_lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	dma_receipt.handoff.base = 0x400000;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	dma_binding.current.invocation_generation = 6;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	assert(!memcmp(&receipt, &(struct bootmem_reservation_receipt){ 0 },
		sizeof(receipt)));
	assert(!starbook_mtl_lifecycle_mailbox_dma_protected(&binding,
		0x400000, 0x1000));
	prepare(&receipt);
	dma_binding.current.loader_instance_nonce.low++;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	dma_binding.current.loader_lifecycle = SMM_INVOCATION_LOADER_S3_RELOAD;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	reject_dma = true;
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	unprotected = &binding;
	unprotected_size = sizeof(binding);
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) == CB_ERR);
	prepare(&receipt);
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops,
		(void *)&receipt) == CB_ERR);
}

static void callback_hostility(void)
{
	struct bootmem_reservation_receipt receipt;
	struct starbook_mtl_lifecycle_mailbox_binding binding = { 0 };
	struct starbook_mtl_lifecycle_mailbox_binding mutated;
	struct smm_invocation_save_state_ops ops = { 0 };
	uint8_t frame[4096];

	prepare(&receipt);
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) ==
		CB_SUCCESS);
	mutated = binding;
	mutated.invocation_generation++;
	assert(!starbook_mtl_lifecycle_mailbox_dma_protected(&mutated,
		0x400000, 0x1000));
	prepare(&receipt);
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) ==
		CB_SUCCESS);
	unprotected = &binding;
	unprotected_size = sizeof(binding);
	assert(!starbook_mtl_lifecycle_mailbox_dma_protected(&binding,
		0x400000, 0x1000));
	prepare(&receipt);
	assert(starbook_mtl_lifecycle_mailbox_verify_consume(&receipt,
		0x400000, 0x1000, frame, sizeof(frame), &ops, &binding) ==
		CB_SUCCESS);
	reenter_dma = true;
	reenter_context = &binding;
	assert(starbook_mtl_lifecycle_mailbox_dma_protected(&binding,
		0x400000, 0x1000));
	assert(!nested_result);
}

int main(void)
{
	success_and_runtime_reproof();
	rejects_hostile_inputs();
	callback_hostility();
	return 0;
}
