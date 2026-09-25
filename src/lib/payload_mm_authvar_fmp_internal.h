/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_AUTHVAR_FMP_INTERNAL_H
#define LIB_PAYLOAD_MM_AUTHVAR_FMP_INTERNAL_H

#include "payload_mm_fmp_owner_authvar_internal.h"

enum payload_mm_authvar_fmp_operation {
	PAYLOAD_MM_AUTHVAR_FMP_READ_STATE = 1,
	PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE,
};

/* Exact FmpState transaction; observation is published only after media end. */
uint64_t payload_mm_authvar_fmp_state_transaction(
	enum payload_mm_authvar_fmp_operation operation,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate,
	struct payload_mm_fmp_owner_record *observation);

/* One-shot legacy import and cleanup; result is published after media end. */
uint64_t payload_mm_authvar_fmp_state_initialize(
	struct payload_mm_fmp_owner_record *observation);

/* A transient pre-reconciliation failure may be attempted again this boot. */
bool payload_mm_authvar_fmp_state_reconciliation_retryable(void);

/* Seed the owner while reconciliation is sealed but normal access is closed. */
uint64_t payload_mm_authvar_fmp_state_activation_read(
	struct payload_mm_fmp_owner_record *observation);

/* Open normal FMP state transactions after the checked owner is installed. */
enum cb_err payload_mm_authvar_fmp_state_activate(void);

/* Permanently close the executor after an ambiguous owner-install outcome. */
void payload_mm_authvar_fmp_state_activation_abort(void);

#if ENV_TEST
struct payload_mm_authvar_fmp_layout_test {
	size_t required_size;
	size_t offsets[4];
	size_t protected_count;
	size_t protected_offsets[16];
	size_t protected_sizes[16];
};
bool payload_mm_authvar_fmp_test_layout(
	struct payload_mm_authvar_fmp_layout_test *layout);
void payload_mm_authvar_fmp_test_corrupt_offset(unsigned int part, bool sealed);
void payload_mm_authvar_fmp_test_mutate_workspace(unsigned int part);
void payload_mm_authvar_fmp_test_mutate_record(size_t offset);
void payload_mm_authvar_fmp_test_mutate_control(unsigned int part);
void payload_mm_authvar_fmp_test_mutate_phase(bool sealed);
unsigned int payload_mm_authvar_fmp_test_phase(void);
void payload_mm_authvar_fmp_test_force_active(void);
bool payload_mm_authvar_fmp_test_installed(void);
bool payload_mm_authvar_fmp_test_record_binding_active(void);
bool payload_mm_authvar_fmp_test_record_target(const void *buffer, size_t size,
	bool tail, bool padding, size_t *buffer_offset, size_t *record_offset,
	bool *separate_image);
#endif

#endif
