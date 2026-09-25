/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef MAINBOARD_EMULATION_QEMU_Q35_VTD_REGISTERS_H
#define MAINBOARD_EMULATION_QEMU_Q35_VTD_REGISTERS_H

#include <stdint.h>

#define Q35_VTD_VERSION 0x00U
#define Q35_VTD_CAP 0x08U
#define Q35_VTD_ECAP 0x10U
#define Q35_VTD_GCMD 0x18U
#define Q35_VTD_GSTS 0x1cU
#define Q35_VTD_RTADDR 0x20U
#define Q35_VTD_CCMD 0x28U
#define Q35_VTD_FSTS 0x34U
#define Q35_VTD_PMEN 0x64U
#define Q35_VTD_PLMBASE 0x68U
#define Q35_VTD_PLMLIMIT 0x6cU
#define Q35_VTD_PHMBASE 0x70U
#define Q35_VTD_PHMLIMIT 0x78U
#define Q35_VTD_ROOT_SET (1U << 30)
#define Q35_VTD_TRANSLATION_ENABLE (1U << 31)
#define Q35_VTD_FAULT_PENDING (1U << 1)
#define Q35_VTD_PMR_ENABLE (1U << 31)
#define Q35_VTD_PMR_STATUS (1U << 0)

struct q35_vtd_io {
	void *context;
	uint32_t (*read32)(void *context, uint32_t offset);
	void (*write32)(void *context, uint32_t offset, uint32_t value);
	void (*commit_tables)(void *context);
};

int q35_vtd_default_deny(const struct q35_vtd_io *io, uint32_t root_phys);
int q35_vtd_switch_root(const struct q35_vtd_io *io,
	uint32_t expected_root_phys, uint32_t root_phys);
int q35_vtd_invalidate(const struct q35_vtd_io *io);

#endif
