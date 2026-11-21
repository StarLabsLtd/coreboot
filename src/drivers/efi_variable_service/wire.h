/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef EFI_VARIABLE_SERVICE_WIRE_H
#define EFI_VARIABLE_SERVICE_WIRE_H

#include <types.h>
#include <Uefi/UefiBaseType.h>

/* Both payloads use the IA32 MM layout, even when running in x86-64 mode. */
struct mm_communication_header32 {
	EFI_GUID guid;
	uint32_t message_length;
	uint8_t data[];
} __packed;

struct smm_variable_header32 {
	uint32_t function;
	uint32_t return_status;
	uint8_t data[];
} __packed;

struct smm_variable_access32 {
	EFI_GUID guid;
	uint32_t data_size;
	uint32_t name_size;
	uint32_t attributes;
	CHAR16 name[];
} __packed;

#define SMM_VARIABLE_FUNCTION_GET_VARIABLE 1
#define SMM_VARIABLE_FUNCTION_SET_VARIABLE 3

_Static_assert(sizeof(struct mm_communication_header32) == 20,
	       "Unexpected MM communication header size");
_Static_assert(sizeof(struct smm_variable_header32) == 8,
	       "Unexpected MM variable header size");
_Static_assert(sizeof(struct smm_variable_access32) == 28,
	       "Unexpected MM variable access size");

#endif
