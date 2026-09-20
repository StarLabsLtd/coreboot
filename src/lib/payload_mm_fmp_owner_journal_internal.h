/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LIB_PAYLOAD_MM_FMP_OWNER_JOURNAL_INTERNAL_H
#define LIB_PAYLOAD_MM_FMP_OWNER_JOURNAL_INTERNAL_H

#include "payload_mm_fmp_owner_internal.h"
#include "payload_mm_fmp_owner_layout_internal.h"

#define PAYLOAD_MM_FMP_OWNER_JOURNAL_REVISION 1U
#define PAYLOAD_MM_FMP_OWNER_JOURNAL_FORMAT 1U
#define PAYLOAD_MM_FMP_OWNER_JOURNAL_DOMAINS 2U
#define PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS 5U
#define PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE 32U
#define PAYLOAD_MM_FMP_OWNER_JOURNAL_CONTEXT_SIZE 128U
#define PAYLOAD_MM_FMP_OWNER_JOURNAL_MAX_SLOTS 32U
#define PAYLOAD_MM_FMP_OWNER_PREPARED_REVISION 1U
#define PAYLOAD_MM_FMP_OWNER_PREPARED_STATE 0xfffffffeU
#define PAYLOAD_MM_FMP_OWNER_COMMITTED_STATE 0xfffffffcU
#define PAYLOAD_MM_FMP_OWNER_JOURNAL_MAGIC 0x314c4e4a504d4d50ULL
#define PAYLOAD_MM_FMP_OWNER_PREPARED_MAGIC 0x31504552504d4d50ULL

struct payload_mm_fmp_owner_journal_anchor {
	uint64_t epoch;
	uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
} __aligned(8);

struct payload_mm_fmp_owner_journal_manifest {
	uint64_t magic;
	uint32_t revision;
	uint32_t size;
	uint32_t slot_size;
	uint32_t owner_record_size;
	uint32_t owner_record_count;
	uint32_t format;
	uint64_t epoch;
	uint8_t storage_domain[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	uint8_t identity_binding[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE];
	struct payload_mm_fmp_owner_record
		record[PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS];
} __aligned(8);

/* Append-only companion to a candidate manifest; legacy slots leave it erased. */
struct payload_mm_fmp_owner_prepared {
	uint64_t magic;
	uint32_t revision;
	uint32_t size;
	uint32_t state;
	uint32_t reserved;
	uint64_t generation;
	uint64_t transaction;
	struct payload_mm_fmp_owner_journal_anchor current;
	struct payload_mm_fmp_owner_journal_anchor candidate;
} __aligned(8);

_Static_assert(sizeof(struct payload_mm_fmp_owner_journal_anchor) == 40,
	"Payload-MM FMP owner journal anchor layout");
_Static_assert(sizeof(struct payload_mm_fmp_owner_journal_manifest) == 344,
	"Payload-MM FMP owner journal manifest layout");
_Static_assert(sizeof(struct payload_mm_fmp_owner_prepared) == 120,
	"Payload-MM FMP owner prepared layout");

bool payload_mm_fmp_owner_journal_anchor_valid(
	const struct payload_mm_fmp_owner_journal_anchor *anchor);
bool payload_mm_fmp_owner_journal_anchor_equal(
	const struct payload_mm_fmp_owner_journal_anchor *left,
	const struct payload_mm_fmp_owner_journal_anchor *right);
bool payload_mm_fmp_owner_journal_manifest_shape_valid(
	const struct payload_mm_fmp_owner_journal_manifest *manifest,
	u32 slot_size);
bool payload_mm_fmp_owner_journal_manifest_records_valid(
	const struct payload_mm_fmp_owner_journal_manifest *manifest);
bool payload_mm_fmp_owner_journal_prepared_shape_valid(
	const struct payload_mm_fmp_owner_prepared *prepared);

typedef enum cb_err payload_mm_fmp_owner_journal_read_fn(const void *context,
	uint64_t offset, void *buffer, size_t size);
typedef enum cb_err payload_mm_fmp_owner_journal_program_fn(const void *context,
	uint64_t offset, const void *buffer, size_t size);
typedef enum cb_err payload_mm_fmp_owner_journal_erase_fn(const void *context,
	uint64_t offset, size_t size);
typedef enum cb_err payload_mm_fmp_owner_journal_sync_fn(const void *context);
typedef enum cb_err payload_mm_fmp_owner_journal_sha256_fn(const void *context,
	const void *buffer, size_t size,
	uint8_t digest[PAYLOAD_MM_FMP_OWNER_JOURNAL_DIGEST_SIZE]);
typedef enum cb_err payload_mm_fmp_owner_journal_anchor_read_fn(
	const void *context, struct payload_mm_fmp_owner_journal_anchor *anchor);
/* The return value is advisory. A fresh anchor read decides the outcome. */
typedef enum cb_err payload_mm_fmp_owner_journal_anchor_advance_fn(
	const void *context,
	const struct payload_mm_fmp_owner_journal_anchor *current,
	const struct payload_mm_fmp_owner_journal_anchor *candidate);
/* Provisioning is deliberately a separate factory-only operation. */
typedef enum cb_err payload_mm_fmp_owner_journal_anchor_provision_fn(
	const void *context,
	const struct payload_mm_fmp_owner_journal_anchor *candidate);

struct payload_mm_fmp_owner_journal_port {
	uint32_t revision;
	uint32_t size;
	struct fmp_owner_layout layout;
	payload_mm_fmp_owner_journal_read_fn *read;
	payload_mm_fmp_owner_journal_program_fn *program;
	payload_mm_fmp_owner_journal_erase_fn *erase;
	payload_mm_fmp_owner_journal_sync_fn *sync;
	payload_mm_fmp_owner_journal_sha256_fn *sha256;
	payload_mm_fmp_owner_journal_anchor_read_fn *anchor_read;
	payload_mm_fmp_owner_journal_anchor_advance_fn *anchor_advance;
	payload_mm_fmp_owner_journal_anchor_provision_fn *anchor_provision;
	const void *context;
	size_t context_size;
};

enum cb_err payload_mm_fmp_owner_journal_install(
	const struct payload_mm_fmp_owner_journal_port *trusted_port,
	payload_mm_authvar_protected_storage storage_is_protected, void *context);
enum cb_err payload_mm_fmp_owner_journal_backend(
	struct payload_mm_fmp_owner_backend *backend);
enum cb_err payload_mm_fmp_owner_journal_factory_provision(
	const struct payload_mm_fmp_owner_record
		record[PAYLOAD_MM_FMP_OWNER_JOURNAL_KEYS]);
#if CONFIG(CAPSULE_TPM_ANCHOR_GRANT)
enum cb_err payload_mm_fmp_owner_journal_prepare(
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate,
	uint64_t generation, uint64_t transaction);
enum cb_err payload_mm_fmp_owner_journal_reconcile_prepared(void);
#endif

#endif
