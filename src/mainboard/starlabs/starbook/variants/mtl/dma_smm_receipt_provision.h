/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_DMA_SMM_RECEIPT_PROVISION_H

#include "dma_smm_policy.h"

#include <cpu/x86/smm_invocation_evidence.h>
#include <cpu/x86/smm_invocation_entry.h>
#include <cpu/x86/smm_command.h>
#if CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) && CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
#include <bootmem_reservation_receipt.h>
#endif

#define STARBOOK_MTL_DMA_RECEIPT_FRAME_REVISION 1U
#define STARBOOK_MTL_DMA_RECEIPT_FRAME_REQUEST 1U
#define STARBOOK_MTL_DMA_RECEIPT_FRAME_ACCEPTED 2U
#define STARBOOK_MTL_DMA_RECEIPT_FRAME_REJECTED 3U
#define STARBOOK_MTL_DMA_RECEIPT_WIRE_REQUEST 0x525043fdU
#define STARBOOK_MTL_DMA_RECEIPT_WIRE_SUCCESS 0x52504f4bU
#define STARBOOK_MTL_DMA_RECEIPT_DEPENDENCIES_REVISION 1U

struct starbook_mtl_dma_receipt_dependencies {
	uint32_t revision;
	uint32_t size;
	void *context;
	size_t context_size;
	bool (*ordinary_dram_range)(void *context, uint64_t base, size_t size);
};

struct starbook_mtl_dma_receipt_frame {
	uint32_t revision;
	uint32_t size;
	uint32_t state;
	uint32_t reserved;
	struct starbook_mtl_dma_smm_receipt candidate;
} __aligned(8);

struct starbook_mtl_dma_smm_binding {
	const struct starbook_mtl_dma_smm_receipt *receipt;
};

struct starbook_mtl_dma_smm_epoch_range {
	uint64_t base;
	uint64_t size;
};

enum cb_err starbook_mtl_dma_receipt_provision_send(void);
#if CONFIG(STARLABS_STARBOOK_MTL_AUTHVAR_PRESENCE_S3_REARM)
bool starbook_mtl_dma_receipt_provisioned(void);
#endif
bool starbook_mtl_dma_receipt_transport_frame(uintptr_t *base, size_t *size);
enum cb_err starbook_mtl_dma_receipt_candidate_build(
	struct starbook_mtl_dma_smm_receipt *candidate);

#if ENV_SMM || ENV_TEST
enum cb_err starbook_mtl_dma_receipt_policy(
	const struct starbook_mtl_dma_receipt_dependencies **dependencies);
enum cb_err starbook_mtl_dma_receipt_provision_receive(
	const struct smm_invocation_save_state_ops *expected_active_ops);
enum cb_err starbook_mtl_dma_smm_binding_get(
	struct starbook_mtl_dma_smm_binding *binding);
enum cb_err starbook_mtl_dma_smm_epoch_prepare(
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct starbook_mtl_dma_smm_epoch_range ranges[2]);
enum cb_err starbook_mtl_dma_smm_epoch_activate(
	const struct smm_invocation_loader_instance *instance,
	const struct smm_invocation_evidence *evidence,
	const struct smm_invocation_topology *topology,
	const struct smm_invocation_entry_ticket *ticket,
	const struct starbook_mtl_dma_smm_epoch_range ranges[2]);
enum cb_err starbook_mtl_dma_smm_epoch_retain(void);
bool starbook_mtl_dma_smm_epoch_range_protected(
	void *unused, uint64_t base, uint64_t size);
bool starbook_mtl_dma_smm_cold_range_protected(
	void *unused, uint64_t base, uint64_t size);
void starbook_mtl_dma_smm_epoch_poison(void);
#if CONFIG(PAYLOAD_BOOT_PRIVATE_BUFFER) && CONFIG(PAYLOAD_MM_AUTHVAR_SERVICE_ROUTE_ATTESTED)
/*
 * Unselected SMM-only prerequisite. These operations establish no authority
 * for a resumed CPU, external code, an OS variable reply, or S3. Protected
 * first-entry receipt delivery and terminal loader lifetime remain separate.
 */
enum cb_err starbook_mtl_boot_private_lease_prepare(
	struct bootmem_reservation_receipt_authority *verifier,
	struct bootmem_reservation_receipt *receipt);
enum cb_err starbook_mtl_boot_private_lease_begin_held(void);
enum cb_err starbook_mtl_boot_private_lease_recheck_held(void);
void starbook_mtl_boot_private_lease_close(void);
#endif
#endif

_Static_assert(sizeof(struct starbook_mtl_dma_receipt_frame) == 6392,
	"MTL DMA receipt frame ABI changed");

#endif
