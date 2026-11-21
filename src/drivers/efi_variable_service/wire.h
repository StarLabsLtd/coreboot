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

struct smm_variable_next32 {
	EFI_GUID guid;
	uint32_t name_size;
	CHAR16 name[];
} __packed;

struct smm_variable_query32 {
	uint64_t maximum_storage;
	uint64_t remaining_storage;
	uint64_t maximum_variable;
	uint32_t attributes;
	uint32_t reserved;
} __packed;

#define SMM_VARIABLE_FUNCTION_GET_VARIABLE 1
#define SMM_VARIABLE_FUNCTION_GET_NEXT_VARIABLE_NAME 2
#define SMM_VARIABLE_FUNCTION_SET_VARIABLE 3
#define SMM_VARIABLE_FUNCTION_QUERY_VARIABLE_INFO 4
#define SMM_VARIABLE_FUNCTION_READY_TO_BOOT 5
#define SMM_VARIABLE_FUNCTION_EXIT_BOOT_SERVICE 6
#define SMM_VARIABLE_FUNCTION_GET_PAYLOAD_SIZE 11

#define ASS_VARIABLE_GUID \
	{ 0xed32d533, 0x99e6, 0x4209, { 0x9c, 0xc0, 0x2d, 0x72, 0xcd, 0xd9, 0x98, 0xa7 } }
#define ASS_SERVICE_READY_GUID \
	{ 0xf4fc98d5, 0xf719, 0x4e49, { 0x88, 0xf4, 0x77, 0xcd, 0xcd, 0xb9, 0xfb, 0x55 } }
#define ASS_VARIABLE_POLICY_GUID \
	{ 0xda1b0d11, 0xd1a7, 0x46c4, { 0x9d, 0xc9, 0xf3, 0x71, 0x48, 0x75, 0xc6, 0xeb } }
#define ASS_END_OF_DXE_GUID \
	{ 0x02ce967a, 0xdd7e, 0x4ffc, { 0x9e, 0xe7, 0x81, 0x0c, 0xf0, 0x47, 0x08, 0x80 } }
#define ASS_READY_TO_LOCK_GUID \
	{ 0x60ff8964, 0xe906, 0x41d0, { 0xaf, 0xed, 0xf2, 0x41, 0xe9, 0x74, 0xe0, 0x8e } }
#define ASS_READY_TO_BOOT_GUID \
	{ 0x7ce88fb3, 0x4bd7, 0x4679, { 0x87, 0xa8, 0xa8, 0xd8, 0xde, 0xe5, 0x0d, 0x2b } }
#define ASS_EXIT_BOOT_SERVICES_GUID \
	{ 0x27abf055, 0xb1b8, 0x4c26, { 0x80, 0x48, 0x74, 0x8f, 0x37, 0xba, 0xa2, 0xdf } }

struct smm_variable_policy32 {
	uint32_t signature;
	uint32_t revision;
	uint32_t command;
	uint32_t result;
	uint8_t data[];
} __packed;

#define ASS_VARIABLE_POLICY_SIGNATURE 0x43504356U
#define ASS_VARIABLE_POLICY_REVISION 1U
enum ass_variable_policy_command {
	ASS_POLICY_DISABLE = 1,
	ASS_POLICY_ENABLED = 2,
	ASS_POLICY_REGISTER = 3,
	ASS_POLICY_LOCK = 5,
};

_Static_assert(sizeof(struct mm_communication_header32) == 20,
	       "Unexpected MM communication header size");
_Static_assert(sizeof(struct smm_variable_header32) == 8,
	       "Unexpected MM variable header size");
_Static_assert(sizeof(struct smm_variable_access32) == 28,
	       "Unexpected MM variable access size");
_Static_assert(sizeof(struct smm_variable_next32) == 20,
	       "Unexpected MM variable enumeration size");
_Static_assert(sizeof(struct smm_variable_query32) == 32,
	       "Unexpected MM variable query size");
_Static_assert(sizeof(struct smm_variable_policy32) == 16,
	       "Unexpected MM variable policy size");

#endif
