/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_LOADER_INSTANCE_SOURCE_H

#include <commonlib/bsd/cb_err.h>
#include <cpu/x86/smm_invocation_loader_identity.h>
#include <stddef.h>
#include <stdint.h>

struct pci_bme_quiesce_snapshot;

#define STARBOOK_MTL_LOADER_INSTANCE_SOURCE_REVISION 2U

enum starbook_mtl_loader_instance_source_state {
	STARBOOK_MTL_LOADER_INSTANCE_SOURCE_EMPTY,
	STARBOOK_MTL_LOADER_INSTANCE_SOURCE_PUBLISHING,
	STARBOOK_MTL_LOADER_INSTANCE_SOURCE_READY,
	STARBOOK_MTL_LOADER_INSTANCE_SOURCE_TAKING,
	STARBOOK_MTL_LOADER_INSTANCE_SOURCE_CONSUMED,
};

struct starbook_mtl_loader_instance_source_payload {
	uint32_t revision;
	uint32_t size;
	uint32_t lifecycle;
	uint32_t sealed;
	struct smm_invocation_loader_instance_nonce loader_instance_nonce;
	uint64_t seal;
	uint32_t presence_boot_class;
	uint32_t reserved;
} __aligned(8);

struct starbook_mtl_loader_instance_source_record {
	uint32_t state;
	uint32_t reserved;
	struct starbook_mtl_loader_instance_source_payload primary;
	struct starbook_mtl_loader_instance_source_payload mirror;
} __aligned(8);

struct starbook_mtl_loader_instance_source_capture {
	uint32_t lifecycle;
	uint32_t captured;
	uint32_t presence_boot_class;
};

struct starbook_mtl_loader_instance_source_ops {
	void *context;
	enum cb_err (*protected_limit)(void *context, uint64_t *exclusive_limit);
	enum cb_err (*quiesce)(void *context);
	enum cb_err (*random128)(void *context,
		struct smm_invocation_loader_instance_nonce *nonce);
};

_Static_assert(sizeof(struct starbook_mtl_loader_instance_source_payload) == 48,
	"MTL loader-instance source payload ABI changed");
_Static_assert(sizeof(struct starbook_mtl_loader_instance_source_record) == 104,
	"MTL loader-instance source record ABI changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_source_record,
	primary) == 8, "MTL loader-instance source record layout changed");
_Static_assert(offsetof(struct starbook_mtl_loader_instance_source_payload,
	loader_instance_nonce) == 16,
	"MTL loader-instance source nonce layout changed");

void starbook_mtl_loader_instance_source_capture(
	struct starbook_mtl_loader_instance_source_capture *capture, int s3wake);
void starbook_mtl_loader_instance_source_capture_classified(
	struct starbook_mtl_loader_instance_source_capture *capture, int s3wake,
	uint32_t presence_boot_class);
enum cb_err starbook_mtl_loader_instance_source_publish(
	struct starbook_mtl_loader_instance_source_capture *capture,
	struct starbook_mtl_loader_instance_source_record *record,
	uintptr_t entry_base, size_t entry_size,
	const struct starbook_mtl_loader_instance_source_ops *ops);
enum cb_err starbook_mtl_loader_instance_source_consume(
	struct starbook_mtl_loader_instance_source_record *record,
	uintptr_t entry_base, size_t entry_size,
	const struct starbook_mtl_loader_instance_source_ops *ops,
	uint32_t *lifecycle,
	struct smm_invocation_loader_instance_nonce *loader_instance_nonce);
enum cb_err starbook_mtl_loader_instance_source_consume_classified(
	struct starbook_mtl_loader_instance_source_record *record,
	uintptr_t entry_base, size_t entry_size,
	const struct starbook_mtl_loader_instance_source_ops *ops,
	uint32_t *lifecycle,
	struct smm_invocation_loader_instance_nonce *loader_instance_nonce,
	uint32_t *presence_boot_class);
struct chipset_power_state;
void mainboard_loader_instance_source_capture(int s3wake,
	const struct chipset_power_state *power_state);
enum cb_err mainboard_loader_instance_source_publish(void);
enum cb_err starbook_mtl_loader_instance_source_ramstage_take(
	uint32_t *lifecycle,
	struct smm_invocation_loader_instance_nonce *loader_instance_nonce,
	struct pci_bme_quiesce_snapshot *snapshot, uint32_t *presence_boot_class);
enum cb_err starbook_mtl_loader_instance_source_ramstage_requiesce(
	uint64_t *protected_limit);

#endif
