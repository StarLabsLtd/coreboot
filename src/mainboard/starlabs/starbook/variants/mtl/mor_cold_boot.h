/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_STARLABS_STARBOOK_MTL_MOR_COLD_BOOT_H
#define MAINBOARD_STARLABS_STARBOOK_MTL_MOR_COLD_BOOT_H

#include "loader_instance_source.h"

struct pci_bme_quiesce_snapshot;

enum starbook_mtl_mor_boot_kind {
	STARBOOK_MTL_MOR_BOOT_UNKNOWN,
	STARBOOK_MTL_MOR_BOOT_COLD,
	STARBOOK_MTL_MOR_BOOT_S3,
};

/* Legacy MOR names remain adapters over the neutral loader-instance source. */
#define STARBOOK_MTL_MOR_COLD_REVISION \
	STARBOOK_MTL_LOADER_INSTANCE_SOURCE_REVISION
#define starbook_mtl_mor_cold_payload \
	starbook_mtl_loader_instance_source_payload
#define starbook_mtl_mor_cold_record \
	starbook_mtl_loader_instance_source_record
#define starbook_mtl_mor_cold_ops starbook_mtl_loader_instance_source_ops

struct starbook_mtl_mor_cold_capture {
	uint32_t lifecycle;
	uint32_t captured;
	uint32_t presence_boot_class;
};

_Static_assert(sizeof(struct starbook_mtl_mor_cold_capture) ==
	sizeof(struct starbook_mtl_loader_instance_source_capture),
	"MTL MOR source capture adapter changed");

void starbook_mtl_mor_cold_capture(
	struct starbook_mtl_mor_cold_capture *capture, int s3wake);
enum cb_err starbook_mtl_mor_cold_publish(
	struct starbook_mtl_mor_cold_capture *capture,
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops);
enum cb_err starbook_mtl_mor_cold_consume(
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops,
	uint64_t *generation);
enum cb_err starbook_mtl_mor_cold_consume_classified(
	struct starbook_mtl_mor_cold_record *record, uintptr_t entry_base,
	size_t entry_size, const struct starbook_mtl_mor_cold_ops *ops,
	uint32_t *boot_kind, uint64_t *generation);
enum cb_err starbook_mtl_mor_cold_ramstage_consume(uint64_t *generation);
enum cb_err starbook_mtl_mor_cold_ramstage_consume_snapshot(
	uint64_t *generation, struct pci_bme_quiesce_snapshot *snapshot);
enum cb_err starbook_mtl_mor_cold_ramstage_classify(
	uint32_t *boot_kind, uint64_t *generation,
	struct pci_bme_quiesce_snapshot *snapshot);

#endif
