/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef TESTS_LIB_Q35_MOR_FIXTURE_H
#define TESTS_LIB_Q35_MOR_FIXTURE_H

#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_mor_probe.h>
#include <commonlib/bsd/cb_err.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define Q35_MOR_FIXTURE_BLOCK_SIZE 4096U
#define Q35_MOR_FIXTURE_BLOCKS 4U
#define Q35_MOR_FIXTURE_SIZE \
	(Q35_MOR_FIXTURE_BLOCK_SIZE * Q35_MOR_FIXTURE_BLOCKS)

enum q35_mor_fixture_kind {
	Q35_MOR_FIXTURE_ASSERTED_UPPER_BITS,
	Q35_MOR_FIXTURE_NO_REQUEST,
	Q35_MOR_FIXTURE_ABSENT,
	Q35_MOR_FIXTURE_MALFORMED,
	Q35_MOR_FIXTURE_RECOVERY,
	Q35_MOR_FIXTURE_FAULT,
	Q35_MOR_FIXTURE_IDEMPOTENT_BEFORE,
	Q35_MOR_FIXTURE_IDEMPOTENT_AFTER,
	Q35_MOR_FIXTURE_S3,
	Q35_MOR_FIXTURE_COUNT,
};

struct q35_mor_fixture_expectation {
	enum cb_err probe_status;
	enum cb_err ftw_status;
	struct payload_mm_authvar_mor_entry entry;
	enum payload_mm_authvar_ftw_action ftw_action;
	bool resume_from_s3;
	bool clear_allowed;
};

/*
 * Deterministically compose one complete EDK2 26.09-compatible SMMSTORE image
 * and its platform expectation.  No checked-in binary seed is required.
 */
bool q35_mor_fixture_generate(enum q35_mor_fixture_kind kind,
	uint8_t media[Q35_MOR_FIXTURE_SIZE],
	struct q35_mor_fixture_expectation *expectation);

#endif /* TESTS_LIB_Q35_MOR_FIXTURE_H */
