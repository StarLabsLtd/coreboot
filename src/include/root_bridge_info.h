/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef ROOT_BRIDGE_INFO_H
#define ROOT_BRIDGE_INFO_H

#include <types.h>

/* Universal Payload PCI Root Bridges Info, revision 1. */
struct root_bridge_aperture {
	uint64_t base;
	uint64_t limit;
	uint64_t translation;
} __packed;

struct pci_root_bridge_info {
	uint32_t segment;
	uint64_t supports;
	uint64_t attributes;
	bool dma_above4g;
	bool no_ext_config_space;
	uint64_t allocation_attributes;
	struct root_bridge_aperture bus;
	struct root_bridge_aperture io;
	struct root_bridge_aperture mem;
	struct root_bridge_aperture mem_above4g;
	struct root_bridge_aperture pmem;
	struct root_bridge_aperture pmem_above4g;
	uint32_t hid;
	uint32_t uid;
} __packed;

struct pci_root_bridges_info {
	uint8_t revision;
	uint8_t reserved;
	uint16_t length;
	bool resource_assigned;
	uint8_t count;
	struct pci_root_bridge_info root_bridge[];
} __packed;

#endif
