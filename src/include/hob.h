/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef HAND_OFF_BLOCK_H
#define HAND_OFF_BLOCK_H

#include <string.h>
#include <Uefi.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include <Library/HobLib.h>

EFI_HOB_HANDOFF_INFO_TABLE *HobTableInit(VOID *MemoryBegin, UINTN MemoryLength,
	VOID *EfiFreeMemoryBottom, VOID *EfiFreeMemoryTop);

void *build_payload_hobs(void);

static inline int CompareGuid(const EFI_GUID *guid1, const EFI_GUID *guid2)
{
	return !memcmp(guid1, guid2, sizeof(EFI_GUID));
}

static inline EFI_GUID *CopyGuid(EFI_GUID *dest, const EFI_GUID *src)
{
	return (EFI_GUID *)memcpy(dest, src, sizeof(EFI_GUID));
}

#endif /* PROGRAM_LOADING_H */
