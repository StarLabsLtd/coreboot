/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_OWNER_AUTHVAR_BOOT_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_OWNER_AUTHVAR_BOOT_INTERNAL_H

#include "payload_mm_fmp_owner_authvar_internal.h"
#include <boot/payload_mm_fmp_boot.h>

#if ENV_TEST
void payload_mm_fmp_owner_authvar_boot_test_corrupt_control(bool sealed);
void payload_mm_fmp_owner_authvar_boot_test_corrupt_phase(bool sealed);
void payload_mm_fmp_owner_authvar_boot_test_corrupt_proof(bool sealed);
void payload_mm_fmp_owner_authvar_boot_test_corrupt_contract(bool sealed);
void payload_mm_fmp_owner_authvar_boot_test_corrupt_identity(bool sealed);
void payload_mm_fmp_owner_authvar_boot_test_corrupt_current(bool sealed);
void payload_mm_fmp_owner_authvar_boot_test_corrupt_pending(bool sealed);
#endif

#endif
