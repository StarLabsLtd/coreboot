/** @file
  Helpers for enforcing a firmware-owned Secure Boot policy.

  Copyright (c) 2026, Star Labs Systems. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef STATIC_SECURE_BOOT_POLICY_LIB_H_
#define STATIC_SECURE_BOOT_POLICY_LIB_H_

/**
  Verify that a signature database contains every required entry.

  Both buffers must contain well-formed EFI_SIGNATURE_LIST sequences.
  Additional entries in Database are allowed.

  @retval EFI_SUCCESS             Every required entry is present.
  @retval EFI_SECURITY_VIOLATION  A required entry is absent.
  @retval EFI_COMPROMISED_DATA    Either database is malformed.
  @retval EFI_INVALID_PARAMETER   A buffer is NULL or a size is zero.
**/
EFI_STATUS
EFIAPI
StaticSecureBootDatabaseContains (
  IN CONST UINT8  *Required,
  IN UINTN        RequiredSize,
  IN CONST UINT8  *Database,
  IN UINTN        DatabaseSize
  );


#endif
