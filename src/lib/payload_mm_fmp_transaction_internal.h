/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_TRANSACTION_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_TRANSACTION_INTERNAL_H

#include <boot/payload_mm_authvar.h>

enum cb_err payload_mm_fmp_transaction_install(
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
enum cb_err payload_mm_fmp_transaction_execute(uint64_t request_address);
enum cb_err payload_mm_fmp_transaction_execute_intent(
	const struct payload_mm_fmp_capsule_intent *intent);
void payload_mm_fmp_transaction_close(void);

#if ENV_TEST
const void *payload_mm_fmp_transaction_test_authority(size_t *size);
#endif

#endif
