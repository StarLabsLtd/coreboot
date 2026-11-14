/** @file
  Secure Boot signature-database policy helpers.

  Copyright (c) 2026, Star Labs Systems. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Guid/ImageAuthentication.h>
#include <Library/BaseMemoryLib.h>
#include "database.h"

STATIC
EFI_STATUS
GetSignatureList (
  IN CONST UINT8                *Data,
  IN UINTN                      DataSize,
  IN OUT UINTN                  *Offset,
  OUT CONST EFI_SIGNATURE_LIST  **SignatureList
  )
{
  CONST EFI_SIGNATURE_LIST  *List;
  UINTN                     EntryBytes;
  UINTN                     HeaderBytes;

  if ((*Offset > DataSize) ||
      (DataSize - *Offset < sizeof (EFI_SIGNATURE_LIST)))
  {
    return EFI_COMPROMISED_DATA;
  }

  List = (CONST EFI_SIGNATURE_LIST *)(Data + *Offset);
  if ((List->SignatureListSize < sizeof (EFI_SIGNATURE_LIST)) ||
      (List->SignatureListSize > DataSize - *Offset))
  {
    return EFI_COMPROMISED_DATA;
  }

  HeaderBytes = sizeof (EFI_SIGNATURE_LIST) + List->SignatureHeaderSize;
  if ((HeaderBytes < sizeof (EFI_SIGNATURE_LIST)) ||
      (HeaderBytes > List->SignatureListSize) ||
      (List->SignatureSize < sizeof (EFI_GUID)))
  {
    return EFI_COMPROMISED_DATA;
  }

  EntryBytes = List->SignatureListSize - HeaderBytes;
  if ((EntryBytes == 0) || ((EntryBytes % List->SignatureSize) != 0)) {
    return EFI_COMPROMISED_DATA;
  }

  *SignatureList = List;
  *Offset       += List->SignatureListSize;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ValidateSignatureDatabase (
  IN CONST UINT8  *Database,
  IN UINTN        DatabaseSize
  )
{
  CONST EFI_SIGNATURE_LIST  *List;
  EFI_STATUS                Status;
  UINTN                     ListOffset;

  ListOffset = 0;
  while (ListOffset < DatabaseSize) {
    Status = GetSignatureList (Database, DatabaseSize, &ListOffset, &List);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
SignatureEntryIsPresent (
  IN CONST EFI_SIGNATURE_LIST  *RequiredList,
  IN CONST UINT8               *RequiredEntry,
  IN CONST UINT8               *Database,
  IN UINTN                     DatabaseSize
  )
{
  CONST EFI_SIGNATURE_LIST  *List;
  EFI_STATUS                Status;
  UINTN                     EntryOffset;
  UINTN                     ListOffset;

  ListOffset = 0;
  while (ListOffset < DatabaseSize) {
    Status = GetSignatureList (Database, DatabaseSize, &ListOffset, &List);
    if (EFI_ERROR (Status)) {
      return FALSE;
    }

    if (!CompareGuid (&List->SignatureType, &RequiredList->SignatureType) ||
        (List->SignatureHeaderSize != RequiredList->SignatureHeaderSize) ||
        (List->SignatureSize != RequiredList->SignatureSize) ||
        (CompareMem (
           (CONST UINT8 *)List + sizeof (EFI_SIGNATURE_LIST),
           (CONST UINT8 *)RequiredList + sizeof (EFI_SIGNATURE_LIST),
           List->SignatureHeaderSize
           ) != 0))
    {
      continue;
    }

    EntryOffset = sizeof (EFI_SIGNATURE_LIST) + List->SignatureHeaderSize;
    while (EntryOffset < List->SignatureListSize) {
      if (CompareMem (
            (CONST UINT8 *)List + EntryOffset,
            RequiredEntry,
            List->SignatureSize
            ) == 0)
      {
        return TRUE;
      }

      EntryOffset += List->SignatureSize;
    }
  }

  return FALSE;
}

EFI_STATUS
EFIAPI
StaticSecureBootDatabaseContains (
  IN CONST UINT8  *Required,
  IN UINTN        RequiredSize,
  IN CONST UINT8  *Database,
  IN UINTN        DatabaseSize
  )
{
  CONST EFI_SIGNATURE_LIST  *List;
  EFI_STATUS                Status;
  UINTN                     EntryOffset;
  UINTN                     ListOffset;

  if ((Required == NULL) || (Database == NULL) ||
      (RequiredSize == 0) || (DatabaseSize == 0))
  {
    return EFI_INVALID_PARAMETER;
  }

  Status = ValidateSignatureDatabase (Required, RequiredSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ValidateSignatureDatabase (Database, DatabaseSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ListOffset = 0;
  while (ListOffset < RequiredSize) {
    Status = GetSignatureList (Required, RequiredSize, &ListOffset, &List);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    EntryOffset = sizeof (EFI_SIGNATURE_LIST) + List->SignatureHeaderSize;
    while (EntryOffset < List->SignatureListSize) {
      if (!SignatureEntryIsPresent (
             List,
             (CONST UINT8 *)List + EntryOffset,
             Database,
             DatabaseSize
             ))
      {
        return EFI_SECURITY_VIOLATION;
      }

      EntryOffset += List->SignatureSize;
    }
  }

  return EFI_SUCCESS;
}
