/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_H
#define EFI_VARIABLE_SERVICE_H

#include <stddef.h>
#include <types.h>
#include <commonlib/bsd/cb_err.h>

struct lb_header;
struct smm_runtime;

#define EFI_VARIABLE_SERVICE_REVISION	1
#define EFI_VARIABLE_SERVICE_APM_CMD	0xe7
#define EFI_VARIABLE_SERVICE_STATE_SIGNATURE	0x53564645
#define EFI_VARIABLE_SERVICE_STATE_REVISION	3
#define EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE	0x1000
#define EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY	0x20000
#define EFI_VARIABLE_SERVICE_BUFFER_SIZE	\
	(EFI_VARIABLE_SERVICE_BUFFER_HEADER_SIZE + EFI_VARIABLE_SERVICE_MESSAGE_CAPACITY)

#define EFI_VARIABLE_SERVICE_BUFFER_SIGNATURE	0x52415645

enum efi_variable_service_buffer_state {
	EFI_VARIABLE_SERVICE_BUFFER_IDLE,
	EFI_VARIABLE_SERVICE_BUFFER_REQUEST,
	EFI_VARIABLE_SERVICE_BUFFER_RESPONSE,
	EFI_VARIABLE_SERVICE_BUFFER_CONSTRUCTING,
};

struct efi_variable_service_buffer {
	uint32_t signature;
	uint16_t revision;
	uint16_t header_size;
	volatile uint32_t state;
	uint32_t transaction;
	uint32_t message_size;
	uint32_t response_size;
	uint64_t status;
} __packed;

struct efi_mm_communication_status {
	uint8_t valid;
	uint8_t reserved[7];
	uint64_t return_status;
	uint64_t return_buffer_size;
} __packed;

#define EFI_VARIABLE_SERVICE_RET_SUCCESS	0
#define EFI_VARIABLE_SERVICE_RET_FAILURE	1
#define EFI_VARIABLE_SERVICE_RET_UNSUPPORTED	2

struct efi_variable_service_state {
	uint32_t signature;
	uint16_t revision;
	uint16_t size;
	uint32_t fv_base;
	uint32_t fv_size;
	uint32_t core_base;
	uint32_t core_size;
	uint32_t core_entry;
	uint32_t dispatch_entry;
	uint32_t hob_base;
	uint32_t hob_size;
	uint32_t service_info;
	uint32_t spi_mmio_resource;
};

_Static_assert(sizeof(struct efi_variable_service_state) == 48,
	       "Unexpected EFI variable service state size");

_Static_assert(sizeof(struct efi_variable_service_buffer) == 32,
	       "Unexpected EFI variable service buffer size");

_Static_assert(EFI_VARIABLE_SERVICE_BUFFER_SIZE % 0x1000 == 0,
	       "EFI variable service buffer must be page aligned");

void efi_variable_service_load(struct smm_runtime *runtime);
uint32_t efi_variable_service_communicate(void);
enum cb_err efi_variable_service_set_uint(const char *name, uint32_t value);
int efi_variable_service_finalize(void);
void lb_efi_variable_service(struct lb_header *header);

#endif
