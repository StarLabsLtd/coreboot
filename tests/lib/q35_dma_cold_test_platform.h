/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef Q35_DMA_COLD_TEST_PLATFORM_H
#define Q35_DMA_COLD_TEST_PLATFORM_H

#include <types.h>
#include <stdint.h>
/* The native stdint.h aliases, with HOST libc's fixed-width base types. */
typedef uint32_t u32;
typedef uint64_t u64;
#include <commonlib/bsd/cb_err.h>
#include <device/pci_def.h>
#include <device/pci_type.h>
#include <device/resource.h>
#include <boot/capsule_broker_buffers.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "owned-structures.h"
#include "native-entry-point.h"

/* Irrelevant bootstrap wire types are outside this HOST platform model. */
#define MAINBOARD_Q35_NATIVE_SERVICE_H

#define PCI_IO_CONFIG_INDEX 0xcf8U

struct smm_invocation_runtime_view { unsigned int unused; };

uint8_t read8(const void *pointer);
uint16_t read16(const void *pointer);
uint32_t read32(const void *pointer);
uint64_t read64(const void *pointer);
void write16(void *pointer, uint16_t value);
void write32(void *pointer, uint32_t value);
uint32_t inl(uint16_t port);
void outl(uint32_t value, uint16_t port);
uint32_t pci_io_read_config32(pci_devfn_t device, uint16_t offset);
uint8_t pci_io_read_config8(pci_devfn_t device, uint16_t offset);
void udelay(unsigned int microseconds);
void wbinvd(void);
void hlt(void);
void smm_region(uintptr_t *base, size_t *size);
bool platform_payload_mm_authvar_service_finalize_admitted(void);
const volatile struct smm_pci_resource_info *smm_get_pci_resource_store(void);
bool smm_get_dma_owned_memory(const struct smm_dma_owned_memory **output);
void smm_get_capsule_broker_buffers(struct capsule_broker_buffer_reservation *output);
enum cb_err smm_invocation_runtime_view_get(const struct smm_invocation_runtime_view **output);
enum cb_err smm_invocation_runtime_cpu_count(const struct smm_invocation_runtime_view *input,
	uint32_t *cpus);
enum cb_err smm_invocation_runtime_geometry_is_contained(const struct smm_invocation_runtime_view *input,
	uintptr_t base, size_t size);
enum cb_err smm_invocation_runtime_range_is_protected(const struct smm_invocation_runtime_view *input,
	const void *base, size_t size);

#endif
