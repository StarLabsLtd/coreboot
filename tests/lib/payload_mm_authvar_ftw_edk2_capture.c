/* SPDX-License-Identifier: BSD-2-Clause-Patent */

/* Host harness for the exact pinned EDK2 FTW implementation. */

#include "FaultTolerantWrite.h"
#include <Guid/VariableFormat.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 4096U
#define BLOCK_COUNT 3U
#define REGION_SIZE (BLOCK_SIZE * BLOCK_COUNT)
#define FV_HEADER_SIZE 72U
#define STORE_SIZE (BLOCK_SIZE - FV_HEADER_SIZE)

static UINT8 media[REGION_SIZE];
static UINT8 workspace[BLOCK_SIZE];
static const char *output_directory;
static EFI_FTW_DEVICE device;
static EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL working_fvb;
static EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL target_fvb;
static EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL spare_fvb;
static UINT8 spare_backup[BLOCK_SIZE];
static const char *reclaim_capture;

typedef EFI_PHYSICAL_ADDRESS physical_address_t;
typedef CONST GUID const_guid_t;
typedef CONST CHAR8 const_char8_t;
typedef EFI_HANDLE * handle_array_t;

EFI_GUID gEfiCallerIdGuid = {
	0xfe5cea76, 0x4f72, 0x49e8,
	{ 0x98, 0x6f, 0x2c, 0xd8, 0x99, 0xdf, 0xfe, 0x5d },
};
EFI_GUID gEdkiiWorkingBlockSignatureGuid = EDKII_WORKING_BLOCK_SIGNATURE_GUID;
EFI_GUID gEfiAuthenticatedVariableGuid = EFI_AUTHENTICATED_VARIABLE_GUID;

static void save(const char *name)
{
	char path[512];
	FILE *file;

	if (snprintf(path, sizeof(path), "%s/%s", output_directory, name) < 0)
		exit(1);
	file = fopen(path, "wb");
	if (!file || fwrite(media, 1, sizeof(media), file) != sizeof(media) ||
	    fclose(file))
		exit(1);
}

static UINT8 *fvb_bytes(const EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *protocol,
	EFI_LBA lba, UINTN offset, UINTN size)
{
	UINTN base;
	UINT64 position;

	if (protocol == &working_fvb)
		base = 0;
	else if (protocol == &target_fvb)
		base = 0;
	else if (protocol == &spare_fvb)
		base = 2U * BLOCK_SIZE;
	else
		return NULL;
	position = (UINT64)base + lba * BLOCK_SIZE + offset;
	if (offset > BLOCK_SIZE || size > BLOCK_SIZE - offset ||
	    position > REGION_SIZE || size > REGION_SIZE - position)
		return NULL;
	return media + position;
}

static EFI_STATUS EFIAPI get_address(
	const EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *protocol,
	physical_address_t *address)
{
	if (protocol == &target_fvb)
		*address = 0x100000U;
	else if (protocol == &spare_fvb)
		*address = 0x102000U;
	else
		*address = 0x100000U;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI get_block_size(
	const EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *protocol, EFI_LBA lba,
	UINTN *block_size, UINTN *number_of_blocks)
{
	(void)protocol;
	if (lba >= BLOCK_COUNT)
		return EFI_INVALID_PARAMETER;
	*block_size = BLOCK_SIZE;
	*number_of_blocks = BLOCK_COUNT - (UINTN)lba;
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI read_block(
	const EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *protocol, EFI_LBA lba,
	UINTN offset, UINTN *size, UINT8 *buffer)
{
	UINT8 *source = fvb_bytes(protocol, lba, offset, *size);

	if (!source)
		return EFI_INVALID_PARAMETER;
	memcpy(buffer, source, *size);
	return EFI_SUCCESS;
}

static void capture_workspace_write(EFI_LBA lba, UINTN offset, UINTN size)
{
	UINTN absolute = (UINTN)lba * BLOCK_SIZE + offset;
	UINTN header = BLOCK_SIZE + sizeof(EFI_FAULT_TOLERANT_WORKING_BLOCK_HEADER);
	UINTN record = header + sizeof(EFI_FAULT_TOLERANT_WRITE_HEADER);

	if (absolute == header && size == sizeof(EFI_FAULT_TOLERANT_WRITE_HEADER))
		save("t2-header-fe.bin");
	else if (absolute == header && size == 1U && media[header] == 0xfcU)
		save("t3-header-fc.bin");
	else if (absolute == record && size == sizeof(EFI_FAULT_TOLERANT_WRITE_RECORD))
		save("t4-record-ff.bin");
	else if (absolute == record && size == 1U && media[record] == 0xfdU)
		save("t5-record-fd.bin");
	else if (absolute == record && size == 1U && media[record] == 0xf9U)
		save("t6-record-f9.bin");
	else if (absolute == header && size == 1U && media[header] == 0xf8U)
		save("t7-header-f8.bin");
}

static EFI_STATUS EFIAPI write_block(
	const EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *protocol, EFI_LBA lba,
	UINTN offset, UINTN *size, UINT8 *buffer)
{
	UINT8 *destination = fvb_bytes(protocol, lba, offset, *size);

	if (!destination)
		return EFI_INVALID_PARAMETER;
	for (UINTN i = 0; i < *size; i++) {
		if ((destination[i] & buffer[i]) != buffer[i])
			return EFI_DEVICE_ERROR;
		destination[i] &= buffer[i];
	}
	if (protocol == &working_fvb)
		capture_workspace_write(lba, offset, *size);
	if (protocol == &working_fvb && reclaim_capture && offset == 20U &&
	    *size == 1U && media[BLOCK_SIZE + 20U] == 0xfcU)
		save(reclaim_capture);
	return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI erase_blocks(
	const EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *protocol, ...)
{
	va_list arguments;
	EFI_LBA lba;

	va_start(arguments, protocol);
	for (;;) {
		UINTN count;

		lba = va_arg(arguments, EFI_LBA);
		if (lba == EFI_LBA_LIST_TERMINATOR)
			break;
		count = va_arg(arguments, UINTN);
		for (UINTN block = 0; block < count; block++) {
			UINT8 *destination = fvb_bytes(protocol, lba + block, 0,
				BLOCK_SIZE);

			if (!destination) {
				va_end(arguments);
				return EFI_INVALID_PARAMETER;
			}
			memset(destination, 0xff, BLOCK_SIZE);
		}
	}
	va_end(arguments);
	return EFI_SUCCESS;
}

static void initialize_fv(void)
{
	EFI_FIRMWARE_VOLUME_HEADER *fv = (void *)media;
	VARIABLE_STORE_HEADER *store = (void *)(media + FV_HEADER_SIZE);
	UINT16 sum = 0;

	memset(media, 0xff, sizeof(media));
	memset(fv, 0, FV_HEADER_SIZE);
	fv->FileSystemGuid = (EFI_GUID)EFI_SYSTEM_NV_DATA_FV_GUID;
	fv->FvLength = REGION_SIZE;
	fv->Signature = EFI_FVH_SIGNATURE;
	fv->Attributes = 0x00000e36U;
	fv->HeaderLength = FV_HEADER_SIZE;
	fv->Revision = 2U;
	fv->BlockMap[0].NumBlocks = BLOCK_COUNT;
	fv->BlockMap[0].Length = BLOCK_SIZE;
	for (UINTN i = 0; i < FV_HEADER_SIZE; i += sizeof(UINT16))
		sum = (UINT16)(sum + *(UINT16 *)(media + i));
	fv->Checksum = (UINT16)-sum;
	memset(store, 0, sizeof(*store));
	store->Signature = gEfiAuthenticatedVariableGuid;
	store->Size = STORE_SIZE;
	store->Format = 0x5aU;
	store->State = 0xfeU;
}

VOID *EFIAPI CopyMem(VOID *destination, CONST VOID *source, UINTN length)
{
	return memcpy(destination, source, length);
}

VOID *EFIAPI SetMem(VOID *buffer, UINTN size, UINT8 value)
{
	return memset(buffer, value, size);
}

INTN EFIAPI CompareMem(CONST VOID *left, CONST VOID *right, UINTN length)
{
	return memcmp(left, right, length);
}

BOOLEAN EFIAPI CompareGuid(const_guid_t *left, const_guid_t *right)
{
	return memcmp(left, right, sizeof(*left)) == 0;
}

UINT32 FtwCalculateCrc32(VOID *buffer, UINTN size)
{
	UINT32 crc = MAX_UINT32;
	UINT8 *bytes = buffer;

	for (UINTN i = 0; i < size; i++) {
		crc ^= bytes[i];
		for (UINTN bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^ (0xedb88320U &
				(UINT32)-(INT32)(crc & 1U));
	}
	return ~crc;
}

VOID *EFIAPI AllocatePool(UINTN size)
{
	return malloc(size);
}

VOID *EFIAPI AllocateZeroPool(UINTN size)
{
	return calloc(1, size);
}

VOID EFIAPI FreePool(VOID *buffer)
{
	free(buffer);
}

BOOLEAN EFIAPI DebugAssertEnabled(VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintEnabled(VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled(CONST UINTN level)
{
	(void)level;
	return FALSE;
}
VOID EFIAPI DebugAssert(const_char8_t *file, CONST UINTN line,
	const_char8_t *description)
{
	(void)file;
	(void)line;
	(void)description;
	abort();
}
VOID EFIAPI DebugPrint(UINTN level, const_char8_t *format, ...)
{
	(void)level;
	(void)format;
}

BOOLEAN __wrap_IsBootBlock(EFI_FTW_DEVICE *ftw,
	EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *fvb)
{
	(void)ftw;
	(void)fvb;
	return FALSE;
}

EFI_STATUS __wrap_FlushSpareBlockToTargetBlock(EFI_FTW_DEVICE *ftw,
	EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *fvb, EFI_LBA lba, UINTN block_size,
	UINTN number_of_blocks)
{
	(void)fvb;
	(void)lba;
	(void)block_size;
	(void)number_of_blocks;
	memcpy(media, media + 2U * BLOCK_SIZE, ftw->SpareAreaLength);
	return EFI_SUCCESS;
}

EFI_STATUS FtwGetFvbByHandle(EFI_HANDLE handle,
	EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL **fvb)
{
	if (handle != &target_fvb)
		return EFI_NOT_FOUND;
	*fvb = &target_fvb;
	return EFI_SUCCESS;
}

EFI_STATUS FtwGetSarProtocol(VOID **protocol)
{
	*protocol = NULL;
	return EFI_NOT_FOUND;
}

EFI_STATUS GetFvbCountAndBuffer(UINTN *number_handles, handle_array_t *buffer)
{
	*number_handles = 0;
	*buffer = NULL;
	return EFI_NOT_FOUND;
}

static void initialize_protocol(EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *protocol)
{
	memset(protocol, 0, sizeof(*protocol));
	protocol->GetPhysicalAddress = get_address;
	protocol->GetBlockSize = get_block_size;
	protocol->Read = read_block;
	protocol->Write = write_block;
	protocol->EraseBlocks = erase_blocks;
}

static void initialize_device(void)
{
	memset(&device, 0, sizeof(device));
	device.Signature = FTW_DEVICE_SIGNATURE;
	device.FtwWorkSpace = workspace;
	device.FtwWorkSpaceHeader = (void *)workspace;
	device.FtwWorkSpaceSize = BLOCK_SIZE;
	device.WorkBlockSize = BLOCK_SIZE;
	device.FtwWorkBlockLba = 1U;
	device.NumberOfWorkBlock = 1U;
	device.FtwWorkSpaceLba = 1U;
	device.FtwWorkSpaceBase = 0;
	device.FtwFvBlock = &working_fvb;
	device.FtwBackupFvb = &spare_fvb;
	device.SpareAreaAddress = 0x102000U;
	device.SpareAreaLength = BLOCK_SIZE;
	device.SpareBlockSize = BLOCK_SIZE;
	device.NumberOfSpareBlock = 1U;
	device.FtwSpareLba = 0;
	device.FtwWorkSpaceLbaInSpare = 0;
	device.FtwWorkSpaceBaseInSpare = 0;
}

static void initialize_clean_media(void)
{
	EFI_STATUS status;

	initialize_fv();
	InitializeLocalWorkSpaceHeader(BLOCK_SIZE);
	memset(workspace, 0xff, sizeof(workspace));
	status = InitWorkSpaceHeader((void *)workspace);
	if (EFI_ERROR(status))
		exit(1);
	memcpy(media + BLOCK_SIZE, workspace, BLOCK_SIZE);
	initialize_device();
}

static void load_snapshot(const char *directory, const char *name)
{
	char path[512];
	FILE *file;

	if (snprintf(path, sizeof(path), "%s/%s", directory, name) < 0)
		exit(1);
	file = fopen(path, "rb");
	if (!file || fread(media, 1, sizeof(media), file) != sizeof(media) ||
	    fgetc(file) != EOF || fclose(file))
		exit(1);
	initialize_device();
}

static void validate_coreboot_snapshot(const char *directory, const char *name,
	UINT8 header_state, UINT8 spare_state, UINT8 destination_state,
	BOOLEAN restart)
{
	EFI_STATUS status;
	EFI_FAULT_TOLERANT_WRITE_HEADER *header;
	EFI_FAULT_TOLERANT_WRITE_RECORD *record;

	load_snapshot(directory, name);
	status = WorkSpaceRefresh(&device);
	if (EFI_ERROR(status)) {
		fprintf(stderr, "%s: WorkSpaceRefresh %llx\n", name,
			(unsigned long long)status);
		exit(1);
	}
	header = device.FtwLastWriteHeader;
	record = device.FtwLastWriteRecord;
	if (header->HeaderAllocated != header_state ||
	    record->SpareComplete != spare_state ||
	    record->DestinationComplete != destination_state) {
		fprintf(stderr, "%s: header %02x record %02x/%02x\n", name,
			header->HeaderAllocated, record->SpareComplete,
			record->DestinationComplete);
		exit(1);
	}
	if (restart) {
		memcpy(spare_backup, media + 2U * BLOCK_SIZE, BLOCK_SIZE);
		status = FtwRestart(&device.FtwInstance, &target_fvb);
		if (EFI_ERROR(status) || memcmp(media, spare_backup, BLOCK_SIZE)) {
			fprintf(stderr, "%s: FtwRestart %llx\n", name,
				(unsigned long long)status);
			exit(1);
		}
	}
}

static void validate_coreboot_snapshots(const char *directory)
{
	validate_coreboot_snapshot(directory, "coreboot-pre-fd.bin", 0U, 1U, 1U,
		FALSE);
	validate_coreboot_snapshot(directory, "coreboot-fd.bin", 0U, 0U, 1U,
		TRUE);
	validate_coreboot_snapshot(directory, "coreboot-f9.bin", 0U, 0U, 0U,
		FALSE);
	load_snapshot(directory, "coreboot-f8.bin");
	if (EFI_ERROR(WorkSpaceRefresh(&device)) ||
	    device.FtwLastWriteHeader->HeaderAllocated != 1U) {
		fprintf(stderr, "coreboot-f8.bin: completed queue rejected\n");
		exit(1);
	}
}

int main(int argc, char **argv)
{
	EFI_STATUS status;

	if ((argc != 2 && argc != 3) ||
	    sizeof(EFI_FAULT_TOLERANT_WORKING_BLOCK_HEADER) != 32U ||
	    sizeof(EFI_FAULT_TOLERANT_WRITE_HEADER) != 40U ||
	    sizeof(EFI_FAULT_TOLERANT_WRITE_RECORD) != 40U)
		return 1;
	output_directory = argv[1];
	initialize_protocol(&working_fvb);
	initialize_protocol(&target_fvb);
	initialize_protocol(&spare_fvb);
	initialize_clean_media();
	save("t0-clean.bin");
	status = FtwAllocate(&device.FtwInstance, &gEfiCallerIdGuid, 0, 1);
	if (EFI_ERROR(status))
		return 1;
	memcpy(spare_backup, media + 2U * BLOCK_SIZE, BLOCK_SIZE);
	status = FtwWrite(&device.FtwInstance, 0, FV_HEADER_SIZE, STORE_SIZE,
		NULL, &target_fvb, media + FV_HEADER_SIZE);
	if (EFI_ERROR(status))
		return 1;
	if (memcmp(media + 2U * BLOCK_SIZE, spare_backup, BLOCK_SIZE))
		return 1;
	save("t8-spare-erased.bin");

	initialize_clean_media();
	reclaim_capture = "r0-empty-carried.bin";
	status = FtwReclaimWorkSpace(&device, FALSE);
	if (EFI_ERROR(status))
		return 1;
	reclaim_capture = NULL;

	initialize_clean_media();
	status = FtwAllocate(&device.FtwInstance, &gEfiCallerIdGuid, 0, 1);
	if (EFI_ERROR(status))
		return 1;
	reclaim_capture = "r1-abort-old-carried.bin";
	status = FtwReclaimWorkSpace(&device, TRUE);
	if (EFI_ERROR(status))
		return 1;
	reclaim_capture = NULL;
	if (argc == 3)
		validate_coreboot_snapshots(argv[2]);
	return 0;
}
