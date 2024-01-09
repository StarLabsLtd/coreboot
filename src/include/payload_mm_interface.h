/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef PAYLOAD_MM_INTERFACE_H
#define PAYLOAD_MM_INTERFACE_H

#include <commonlib/coreboot_tables.h>
#include <types.h>

/* Payload MM command interface */

#define PAYLOAD_MM_RET_SUCCESS	0
#define PAYLOAD_MM_RET_FAILURE	1

/*
 * The data below describes a load request from the payload MM loader (ring0) to bootloader SMM,
 * and the arguments that the loader wants passed to the payload MM core module.
 */
#define PAYLOAD_MM_CMD_LOAD_AND_CALL_CORE	1

#define PLD_MM_LOAD_CONTEXT_REVISION	1

struct payload_mm_load_context {
	uint16_t  header_size;
	uint8_t   header_revision;
	uint8_t   reserved;
	uint64_t  mm_core_source_address;
	uint32_t  mm_core_destination_address;
	uint32_t  mm_core_size;
	uint32_t  mm_entrypoint_offset;
	uint64_t  mm_entrypoint_arg1;
	uint64_t  mm_entrypoint_arg2;
	uint64_t  mm_entrypoint_arg3;
	uint64_t  implementation_private_data;
} __packed;

/*
 * This memory is the first 4K of the payload MM subregion. It comprises all data shared
 * between coreboot and payload MM, including the physical location of any argument buffers,
 * as payload MM implementations should not require coreboot's handler space to be mapped.
 */
#define PLD_MM_SHARED_MEMORY_MAX_SIZE	4096

/*
 * Used to communicate payload MM loader (ring0) arguments from bootloader SMM to payload MM.
 */
struct payload_mm_core_call_context {
	uint64_t  mm_entrypoint_arg1;
	uint64_t  mm_entrypoint_arg2;
	uint64_t  mm_entrypoint_arg3;
	uint64_t  implementation_private_data;
} __packed;

/*
 * The data below is shared between the bootloader and payload MM in the shared memory, located in
 * the first half of the first 4K of the payload MM subregion. It must be provided by the payload MM at runtime,
 * as it describes the data required by the bootloader to call payload MM.
 */
#define PLD_MM_SHARED_STRUCT_MAGIC	0x5f4d4d5f444c505f  /* '_PLD_MM_' */
#define PLD_MM_SHARED_STRUCT_MAX_SIZE	2048
#define PLD_MM_SHARED_STRUCT_REVISION	1

struct payload_mm_shared_info {
	uint64_t  header_magic;
	uint16_t  shared_info_size;
	uint8_t   header_revision;
	uint8_t   reserved;
	uint32_t  mm_entrypoint_address;
} __packed;

void payload_mm_get_reserved_region(uintptr_t *tseg_base, size_t *tseg_size);

void lb_payload_mm(struct lb_header *header);

uint8_t payload_mm_exec_interface(uint8_t sub_command, void *argument);
void payload_mm_call_entrypoint(void);

#endif
