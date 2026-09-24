/* SPDX-License-Identifier: GPL-2.0-only */

#include <string.h>

#include "../../src/mainboard/starlabs/starbook/variants/mtl/mor_early_dma.h"

#define CHECK(condition) do { if (!(condition)) __builtin_trap(); } while (0)

static struct pci_bme_quiesce_snapshot valid_snapshot(void)
{
	struct pci_bme_quiesce_snapshot snapshot = {
		.bus_count = 4,
		.count = 4,
		.functions = {
			{ .bdf = 0x0000, .vendor = 0x8086, .device = 0x7d01,
			  .command = 3, .class = 0x060000 },
			{ .bdf = 0x0068, .vendor = 0x8086, .device = 0x7ec0,
			  .command = 2, .class = 0x0c0330 },
			{ .bdf = 0x00a0, .vendor = 0x8086, .device = 0x7e7d,
			  .command = 2, .class = 0x0c0330 },
			{ .bdf = 0x0200, .vendor = 0x1d97, .device = 1,
			  .command = 3, .class = 0x010802 },
		},
	};

	return snapshot;
}

int main(int argc, char **argv)
{
	struct pci_bme_quiesce_snapshot snapshot = valid_snapshot();
	struct starbook_mtl_mor_early_dma_record record;
	uint8_t identity[32];
	uint8_t other[32];

	CHECK(argc == 2);
	memset(&record, 0xa5, sizeof(record));
	if (!strcmp(argv[1], "zero-generation"))
		CHECK(starbook_mtl_mor_early_dma_record_build(0, &snapshot,
			&record) != CB_SUCCESS);
	else if (!strcmp(argv[1], "bme")) {
		snapshot.functions[2].command |= 4;
		CHECK(starbook_mtl_mor_early_dma_record_build(1, &snapshot,
			&record) != CB_SUCCESS);
	} else if (!strcmp(argv[1], "order")) {
		snapshot.functions[2].bdf = snapshot.functions[1].bdf;
		CHECK(starbook_mtl_mor_early_dma_record_build(1, &snapshot,
			&record) != CB_SUCCESS);
	} else if (!strcmp(argv[1], "tail")) {
		snapshot.functions[snapshot.count].vendor = 1;
		CHECK(starbook_mtl_mor_early_dma_record_build(1, &snapshot,
			&record) != CB_SUCCESS);
	} else if (!strcmp(argv[1], "input-alias")) {
		CHECK(starbook_mtl_mor_early_dma_record_build(1,
			&record.primary.pci, &record) == CB_ERR_ARG);
	} else if (!strcmp(argv[1], "output-scratch")) {
		record.mirror.pci = snapshot;
		CHECK(starbook_mtl_mor_early_dma_record_build(1,
			&record.mirror.pci, &record) == CB_SUCCESS);
		CHECK(starbook_mtl_mor_early_dma_record_validate(&record, 1,
			&record.primary.pci, identity) == CB_SUCCESS);
	} else {
		CHECK(starbook_mtl_mor_early_dma_record_build(1, &snapshot,
			&record) == CB_SUCCESS);
		CHECK(starbook_mtl_mor_early_dma_record_validate(&record, 1,
			&snapshot, identity) == CB_SUCCESS);
		if (!strcmp(argv[1], "snapshot-drift")) {
			snapshot.functions[3].device++;
			CHECK(starbook_mtl_mor_early_dma_record_validate(&record, 1,
				&snapshot, other) != CB_SUCCESS);
		} else if (!strcmp(argv[1], "generation-drift")) {
			CHECK(starbook_mtl_mor_early_dma_record_validate(&record, 2,
				&snapshot, other) != CB_SUCCESS);
		} else if (!strcmp(argv[1], "primary-mutation")) {
			record.primary.pci.functions[1].class++;
			CHECK(starbook_mtl_mor_early_dma_record_validate(&record, 1,
				&snapshot, other) != CB_SUCCESS);
		} else if (!strcmp(argv[1], "mirror-mutation")) {
			record.mirror.identity[0]++;
			CHECK(starbook_mtl_mor_early_dma_record_validate(&record, 1,
				&snapshot, other) != CB_SUCCESS);
		} else {
			CHECK(!strcmp(argv[1], "valid"));
			CHECK(memcmp(identity, &(const uint8_t[32]) { 0 }, 32));
		}
	}
	return 0;
}
