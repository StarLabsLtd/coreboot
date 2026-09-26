/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_executor.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
#include <payload_mm_cms.h>
#endif
#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_media.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
#include <boot/payload_mm_authvar_record.h>
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
#include <boot/payload_mm_authvar_mor_grant.h>
#include <boot/payload_mm_authvar_mor_identity.h>
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
#include <boot/payload_mm_authvar_signature_db.h>
#endif
#include <boot/payload_mm_authvar_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../../src/lib/payload_mm_authvar_internal.h"
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
#include "../../src/lib/payload_mm_authvar_recovery.h"
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
#include "../../src/lib/payload_mm_authvar_coordinator.h"
#include "../../src/lib/payload_mm_authvar_set_preflight.h"
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER)
#include "../../src/lib/payload_mm_authvar_authority_provider.h"
#endif
#include "payload_mm_authvar_policy_test_provider.h"
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
#include "../../src/lib/payload_mm_authvar_fmp_internal.h"
#endif
#ifdef EXECUTOR_REAL_MEDIA
#include <boot/payload_mm_authvar.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifndef MAP_32BIT
#define MAP_32BIT 0
#endif
#endif

#define BLOCK_SIZE 4096U
#define REGION_SIZE (4U * BLOCK_SIZE)
#define FV_HEADER_SIZE 72U
#define STORE_SIZE (BLOCK_SIZE - FV_HEADER_SIZE)

extern long write(int fd, const void *buffer, unsigned long size);
extern int dprintf(int fd, const char *format, ...);
#ifdef EXECUTOR_REAL_MEDIA
extern pid_t fork(void);
extern void _exit(int status);
#endif

#ifdef EXECUTOR_REAL_MEDIA
static uint8_t *media;
#else
static uint8_t media[REGION_SIZE];
#endif
#define MEDIA_SIZE REGION_SIZE
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
static uint8_t arena[393216] __aligned(__BIGGEST_ALIGNMENT__);
#else
static uint8_t arena[196608] __aligned(__BIGGEST_ALIGNMENT__);
#endif
static bool poisoned;
static bool cache_bound;
static unsigned int begin_count;
static unsigned int end_count;
static unsigned int read_count;
static unsigned int program_count;
static unsigned int erase_count;
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
static bool mor_take_observed;
static enum payload_mm_authvar_mor_seal_test_mutation mor_seal_mutation;
static bool mor_seal_mutate_on_grant;
static bool mor_seal_mutate_on_read;
#endif
struct write_trace_entry {
	uint32_t offset;
	uint32_t size;
	uint8_t first;
};

#ifndef EXECUTOR_REAL_MEDIA
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
static bool write_trace_disjoint(const struct write_trace_entry *trace,
	uint32_t protected_offset, uint32_t protected_size)
{
	uint32_t trace_end;
	uint32_t protected_end;

	if (trace->size > UINT32_MAX - trace->offset ||
	    protected_size > UINT32_MAX - protected_offset)
		return false;
	trace_end = trace->offset + trace->size;
	protected_end = protected_offset + protected_size;
	return trace_end <= protected_offset || protected_end <= trace->offset;
}
#endif
static struct write_trace_entry program_trace[32];
static struct write_trace_entry erase_trace[16];
#endif
static unsigned int fail_closed_count;
#ifndef EXECUTOR_REAL_MEDIA
static bool fake_provider_active;
static bool fake_provider_violation;
#endif
static unsigned int cache_bind_count;
static unsigned int fail_operation;
static unsigned int operation_count;
enum test_callback_kind {
	TEST_CALLBACK_BEGIN = 1,
	TEST_CALLBACK_READ,
	TEST_CALLBACK_PROGRAM,
	TEST_CALLBACK_ERASE,
	TEST_CALLBACK_SYNC,
	TEST_CALLBACK_END,
};
static uint8_t callback_trace[256];
static unsigned int callback_trace_count;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
enum coordinator_media_attack {
	COORDINATOR_MEDIA_ATTACK_NONE,
	COORDINATOR_MEDIA_ATTACK_RESULT,
	COORDINATOR_MEDIA_ATTACK_CONTEXT,
	COORDINATOR_MEDIA_ATTACK_OWNER_DIRTY,
	COORDINATOR_MEDIA_ATTACK_ALTERNATE_OWNER,
};
static enum coordinator_media_attack coordinator_media_attack;
static enum test_callback_kind coordinator_media_attack_callback;
static struct payload_mm_authvar_coordinator_test_result *
	coordinator_media_result;
static struct payload_mm_crypto_owner *coordinator_media_owner;
static struct payload_mm_crypto_owner *coordinator_media_alternate_owner;
static uint8_t *coordinator_media_context;
#endif
#if !defined(EXECUTOR_REAL_MEDIA) || CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
static enum payload_mm_authvar_media_result generic_fault_result =
	PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
static const uint8_t candidate_callback_golden[] = {
	1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 2,
	2, 3, 2, 3, 2, 3, 2, 2, 4, 4, 2, 3, 2, 2, 2, 2, 3, 2, 4, 3,
	2, 2, 3, 2, 3, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 6,
};
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
enum candidate_media_fault_kind {
	CANDIDATE_MEDIA_FAULT_NONE,
	CANDIDATE_MEDIA_FAULT_READ,
	CANDIDATE_MEDIA_FAULT_PROGRAM,
	CANDIDATE_MEDIA_FAULT_ERASE,
};
static enum candidate_media_fault_kind candidate_media_fault;
static enum payload_mm_authvar_media_result candidate_media_fault_result;
static bool candidate_media_fault_armed;
#endif
#ifdef EXECUTOR_REAL_MEDIA
static unsigned int reset_operation;
#endif
static bool mutate_on_program;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
static bool mutate_candidate_header_on_program;
static bool mutate_candidate_source_on_program;
static size_t candidate_image_mutation_offset;
static unsigned int candidate_image_mutation_bit;
#endif
static bool mutate_record_on_read;
static bool mutate_control_on_read;
static bool erase_noop;
static bool record_state_in_body;
static bool corrupt_body_program;
static bool record_state_marker_programmed;
static bool fail_end;
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
static unsigned int fmp_mutate_at_end;
static unsigned int fmp_mutate_workspace_session;
static unsigned int fmp_control_mutate_at_end;
static bool fmp_tandem_end;
static uint32_t fmp_record_media_offset;
#ifndef EXECUTOR_REAL_MEDIA
static size_t fmp_record_canonical_offset;
static bool fmp_record_separate_image;
#endif
static unsigned int fmp_tandem_attack_program_count;
static enum test_callback_kind fmp_fail_callback;
static unsigned int fmp_fail_session;
static unsigned int fmp_tandem_phase;
static bool fmp_tandem_tail;
static bool fmp_tandem_padding;
static bool fmp_sha_fail;
static unsigned int fmp_record_sha_calls;
static bool fmp_require_first_persistence_binding;
static uint8_t fmp_sha_media_before[MEDIA_SIZE];
const void *payload_mm_fmp_test_owner_storage;
size_t payload_mm_fmp_test_owner_storage_size;
uint64_t payload_mm_fmp_test_hardware_instance;
static const void *fmp_reject_smram_span;
static size_t fmp_reject_smram_span_size;
static const void *fmp_reject_media_span;
static size_t fmp_reject_media_span_size;
static bool fmp_reenter_on_begin;
static bool fmp_abort_on_begin;
static unsigned int fmp_activation_read_count;
static unsigned int fmp_initialize_reenter_session;
static unsigned int fmp_mutate_caller;
static unsigned int fmp_mutate_caller_session;
static struct payload_mm_fmp_owner_record *fmp_callback_current;
static struct payload_mm_fmp_owner_record *fmp_callback_candidate;
static struct payload_mm_fmp_owner_record *fmp_callback_published;
static struct payload_mm_fmp_owner_record fmp_nested_output;
static uint64_t fmp_nested_status;
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static bool native_mutate_on_begin;
static struct payload_mm_authvar_policy_request *native_external_request;
static uint8_t *native_external_name;
static uint8_t *native_external_data;
static bool native_collision_on_read;
static unsigned int native_collision_program_count;
static unsigned int native_collision_primary_reads;
static void __maybe_unused native_inject_collision(void);
#endif
static bool corrupt_spare_suffix;
static bool corrupt_ftw_spare_body;
static bool ftw_spare_marker_programmed;
static bool workspace_spare_marker_programmed;
static bool workspace_working_marker_programmed;
static bool corrupt_workspace_spare_body;
static bool corrupt_workspace_spare_suffix;
static bool corrupt_workspace_working_body;
static bool corrupt_queue_header_body;
static bool corrupt_queue_record_body;
static bool corrupt_primary_body;
static bool ftw_header_allocated_marker_programmed;
static bool ftw_destination_marker_programmed;
static bool corrupt_after_final_verify;
static bool corrupt_before_final_verify;
static bool stage_spare_after_final_verify;
static bool corrupt_marker_expected_read;
static bool check_workspace_invalidate_order;
static bool workspace_old_invalid_marker_programmed;
static bool workspace_erased_before_invalidate;
static bool corrupt_erased_spare_readback;
#ifndef EXECUTOR_REAL_MEDIA
static bool erased_spare_readback_pending;
#endif
static bool corrupt_replay_spare_readback;
#ifndef EXECUTOR_REAL_MEDIA
static unsigned int replay_spare_read_count;
#endif
static bool corrupt_replay_primary_readback;
#ifndef EXECUTOR_REAL_MEDIA
static unsigned int replay_primary_read_count;
#endif
static bool check_restore_queue_order;
static bool restore_queue_committed;
static bool spare_erased_before_restore_queue;
static unsigned int primary_erase_count;
static bool reject_precommit_scan;
static unsigned int wrapped_scan_count;
static unsigned int precommit_program_count;
static bool reenter_on_begin;
static uint64_t nested_status;
static uint32_t authority_store_size = REGION_SIZE;
static bool alternate_recovery_plans;
static bool start_alternate_snapshot;
static unsigned int alternate_snapshot_count;
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
static uint8_t *recovery_image_to_mutate;
static size_t recovery_image_to_mutate_size;
static uint32_t recovery_image_mutation_read_offset;
static size_t recovery_image_mutation_read_size;
static unsigned int recovery_image_mutation_matches_to_skip;
#endif
#ifndef EXECUTOR_REAL_MEDIA
static unsigned int tail_read_count;
#endif
#ifndef EXECUTOR_REAL_MEDIA
static unsigned int fake_erased_reads;
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static enum payload_mm_verify_status coordinator_verify_status;
static struct payload_mm_crypto_owner *coordinator_active_owner;

enum payload_mm_verify_status payload_mm_crypto_begin(
	struct payload_mm_crypto_owner *owner)
{
	if (!owner)
		return PAYLOAD_MM_VERIFY_INVALID;
	if (owner->busy || coordinator_active_owner)
		return PAYLOAD_MM_VERIFY_BUSY;
	coordinator_active_owner = owner;
	owner->busy = true;
	owner->arena_used = 0U;
	owner->allocation_count = 0U;
	owner->allocation_failed = false;
	memset(owner->arena, 0, sizeof(owner->arena));
	return PAYLOAD_MM_VERIFY_OK;
}

bool payload_mm_crypto_abort(struct payload_mm_crypto_owner *owner)
{
	if (!owner)
		return false;
	if (coordinator_active_owner && coordinator_active_owner != owner)
		return false;
	memset(owner->arena, 0, sizeof(owner->arena));
	owner->arena_used = 0U;
	owner->allocation_count = 0U;
	owner->allocation_failed = false;
	owner->busy = false;
	coordinator_active_owner = NULL;
	return true;
}

enum payload_mm_verify_status payload_mm_crypto_end(
	struct payload_mm_crypto_owner *owner,
	enum payload_mm_verify_status status)
{
	if (!owner || coordinator_active_owner != owner)
		return PAYLOAD_MM_VERIFY_INTERNAL;
	if (owner->allocation_failed)
		status = PAYLOAD_MM_VERIFY_NO_MEMORY;
	memset(owner->arena, 0, sizeof(owner->arena));
	owner->arena_used = 0U;
	owner->allocation_failed = false;
	owner->busy = false;
	coordinator_active_owner = NULL;
	return status;
}

bool payload_mm_crypto_idle(void)
{
	return coordinator_active_owner == NULL;
}

bool payload_mm_crypto_owner_is_clean(
	const struct payload_mm_crypto_owner *owner)
{
	uint8_t combined = 0U;

	if (!owner || coordinator_active_owner)
		return false;
	for (size_t index = 0U; index < sizeof(owner->arena); index++)
		combined |= owner->arena[index];
	return !owner->arena_used && !owner->busy &&
		!owner->allocation_failed && combined == 0U;
}

bool payload_mm_crypto_abort_active(void)
{
	struct payload_mm_crypto_owner *owner = coordinator_active_owner;

	if (!owner)
		return false;
	memset(owner->arena, 0, sizeof(owner->arena));
	owner->arena_used = 0U;
	owner->allocation_count = 0U;
	owner->allocation_failed = false;
	owner->busy = false;
	coordinator_active_owner = NULL;
	return true;
}

enum payload_mm_verify_status payload_mm_authvar_signature_db_validate(
	struct payload_mm_crypto_owner *owner, const void *data, size_t size,
	enum payload_mm_authvar_signature_db_profile profile,
	uint32_t maximum_x509, struct payload_mm_authvar_signature_db_stats *stats)
{
	(void)owner;
	(void)profile;
	(void)maximum_x509;
	(void)stats;
	if (!data || !size)
		abort();
	return PAYLOAD_MM_VERIFY_OK;
}

enum payload_mm_verify_status payload_mm_authvar_signature_db_filter_append(
	struct payload_mm_crypto_owner *owner, const void *current,
	size_t current_size, const void *append, size_t append_size,
	void *output, size_t output_capacity, size_t *output_size)
{
	(void)owner;
	if (!current || !current_size || !append || !output || !output_size)
		abort();
	if (append_size > output_capacity)
		return PAYLOAD_MM_VERIFY_NO_MEMORY;
	memcpy(output, append, append_size);
	*output_size = append_size;
	return PAYLOAD_MM_VERIFY_OK;
}
#endif

#ifdef EXECUTOR_SCAN_WRAP
enum cb_err __real_payload_mm_authvar_store_scan(
	struct payload_mm_authvar_store_index *index, const void *store,
	size_t store_size, const struct payload_mm_authvar_store_limits *limits);

enum cb_err __wrap_payload_mm_authvar_store_scan(
	struct payload_mm_authvar_store_index *index, const void *store,
	size_t store_size, const struct payload_mm_authvar_store_limits *limits)
{
	enum cb_err result = __real_payload_mm_authvar_store_scan(index, store,
		store_size, limits);

	if (reject_precommit_scan && ++wrapped_scan_count == 2U)
		return CB_ERR;
	return result;
}
#endif

static bool cache_ok(void)
{
#ifdef EXECUTOR_REAL_MEDIA
	return true;
#else
	return cache_bound;
#endif
}

static const uint8_t fv_guid[16] = {
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
};
static const uint8_t store_guid[16] = {
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
};
static const uint8_t work_guid[16] = {
	0x2b, 0x29, 0x58, 0x9e, 0x68, 0x7c, 0x7d, 0x49,
	0xa0, 0xce, 0x65, 0x00, 0xfd, 0x9f, 0x1b, 0x95,
};
static const uint8_t caller_guid[16] = {
	0xf3, 0x2e, 0xa5, 0xc4, 0x27, 0x4e, 0xe2, 0x47,
	0x95, 0x0b, 0xfd, 0xaa, 0xb5, 0x21, 0xb8, 0x95,
};

static void assertion_failed(const char *expression, const char *file, int line)
{
	char message[512];
	int length = snprintf(message, sizeof(message),
		"%s:%d: assertion failed: %s\n", file, line, expression);

	if (length > 0)
		(void)write(2, message, (unsigned long)length);
	abort();
}

#define assert(c) do { if (!(c)) assertion_failed(#c, __FILE__, __LINE__); } while (0)

static void put16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static void put64(uint8_t *p, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static uint32_t test_crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;

	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (unsigned int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320U &
				(uint32_t)-(int32_t)(crc & 1U));
	}
	return ~crc;
}

static void make_fv(uint8_t *bytes)
{
	uint16_t sum = 0;

	memset(bytes, 0xff, BLOCK_SIZE);
	memset(bytes, 0, FV_HEADER_SIZE);
	memcpy(bytes + 16, fv_guid, sizeof(fv_guid));
	put64(bytes + 32, REGION_SIZE);
	put32(bytes + 40, 0x4856465fU);
	put32(bytes + 44, 0x00000e36U);
	put16(bytes + 48, FV_HEADER_SIZE);
	bytes[55] = 2;
	put32(bytes + 56, 4);
	put32(bytes + 60, BLOCK_SIZE);
	for (size_t i = 0; i < FV_HEADER_SIZE; i += 2U)
		sum = (uint16_t)(sum + (uint16_t)bytes[i] +
			((uint16_t)bytes[i + 1U] << 8));
	put16(bytes + 50, (uint16_t)-sum);
	memset(bytes + FV_HEADER_SIZE, 0, 28U);
	memcpy(bytes + FV_HEADER_SIZE, store_guid, sizeof(store_guid));
	put32(bytes + FV_HEADER_SIZE + 16U, STORE_SIZE);
	bytes[FV_HEADER_SIZE + 20U] = 0x5aU;
	bytes[FV_HEADER_SIZE + 21U] = 0xfeU;
}

static void make_workspace(uint8_t *bytes, uint8_t state)
{
	uint8_t header[32];

	memset(bytes, 0xff, BLOCK_SIZE);
	memset(header, 0xff, sizeof(header));
	memcpy(header, work_guid, sizeof(work_guid));
	put64(header + 24, BLOCK_SIZE - sizeof(header));
	put32(header + 16, test_crc32(header, sizeof(header)));
	memcpy(bytes, header, sizeof(header));
	bytes[20] = state;
}

static uint8_t *working(void)
{
	return media + BLOCK_SIZE;
}

static uint8_t *spare(void)
{
	return media + 2U * BLOCK_SIZE;
}

static void make_write(uint8_t record_state, uint8_t header_state)
{
	uint8_t *header = working() + 32U;
	uint8_t *record = header + 40U;

	memset(header, 0xff, 80U);
	header[0] = header_state;
	memcpy(header + 4, payload_mm_authvar_ftw_coreboot_caller_guid,
		sizeof(payload_mm_authvar_ftw_coreboot_caller_guid));
	put64(header + 24, 1);
	put64(header + 32, 0);
	record[0] = record_state;
	put64(record + 8, 0);
	put64(record + 16, FV_HEADER_SIZE);
	put64(record + 24, STORE_SIZE);
	put64(record + 32, (uint64_t)-(int64_t)(2U * BLOCK_SIZE));
}

static void make_clean(void)
{
	memset(media, 0xff, MEDIA_SIZE);
	make_fv(media);
	make_workspace(working(), 0xfeU);
}

static bool fault(enum test_callback_kind kind)
{
	operation_count++;
	assert(callback_trace_count < ARRAY_SIZE(callback_trace));
	callback_trace[callback_trace_count++] = (uint8_t)kind;
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (fmp_fail_callback == kind && fmp_fail_session == begin_count) {
		fmp_fail_callback = 0;
		fmp_fail_session = 0;
		return true;
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (kind == coordinator_media_attack_callback) {
		switch (coordinator_media_attack) {
		case COORDINATOR_MEDIA_ATTACK_RESULT:
			coordinator_media_result->status ^= 1U;
			break;
		case COORDINATOR_MEDIA_ATTACK_CONTEXT:
			coordinator_media_context[0] ^= 1U;
			break;
		case COORDINATOR_MEDIA_ATTACK_OWNER_DIRTY:
			coordinator_media_owner->arena[0] = 0xa5U;
			coordinator_media_owner->arena_used = 1U;
			break;
		case COORDINATOR_MEDIA_ATTACK_ALTERNATE_OWNER:
			assert(payload_mm_crypto_begin(
				coordinator_media_alternate_owner) ==
				PAYLOAD_MM_VERIFY_OK);
			coordinator_media_alternate_owner->arena[0] = 0xa5U;
			coordinator_media_alternate_owner->arena_used = 1U;
			break;
		case COORDINATOR_MEDIA_ATTACK_NONE:
			break;
		}
		coordinator_media_attack = COORDINATOR_MEDIA_ATTACK_NONE;
	}
#endif
#ifdef EXECUTOR_REAL_MEDIA
	if (reset_operation && operation_count == reset_operation)
		_exit(77);
#endif
	return fail_operation && operation_count == fail_operation;
}

#ifndef EXECUTOR_REAL_MEDIA
bool payload_mm_authvar_contract_valid(
	const struct payload_mm_authvar_contract *contract)
{
	return contract && contract->store_size == authority_store_size;
}

bool payload_mm_authvar_authority_snapshot(
	struct payload_mm_authvar_contract *contract)
{
	memset(contract, 0, sizeof(*contract));
	contract->revision = PAYLOAD_MM_AUTHVAR_REVISION;
	contract->size = sizeof(*contract);
	contract->flags = PAYLOAD_MM_AUTHVAR_REQUIRED_FLAGS;
	contract->generation = 1;
	contract->store_size = authority_store_size;
	contract->block_size = BLOCK_SIZE;
	contract->erase_size = BLOCK_SIZE;
	return true;
}

bool payload_mm_authvar_smram_buffer(const void *buffer, size_t size)
{
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (buffer == fmp_reject_smram_span && size == fmp_reject_smram_span_size)
		return false;
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	if (size == sizeof(struct payload_mm_authvar_mor_grant) &&
	    (uintptr_t)buffer >= (uintptr_t)arena &&
	    (uintptr_t)buffer - (uintptr_t)arena <=
		sizeof(arena) - sizeof(struct payload_mm_authvar_mor_grant)) {
		mor_take_observed = true;
		if (mor_seal_mutate_on_grant) {
			assert(payload_mm_authvar_executor_test_mutate_mor_seal(
				mor_seal_mutation));
			mor_seal_mutate_on_grant = false;
		}
	}
#endif
	return buffer && size;
}

bool payload_mm_authvar_buffers_overlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_base = (uintptr_t)left;
	uintptr_t right_base = (uintptr_t)right;

	if (!left_size || !right_size)
		return false;
	if (!left || !right || left_size - 1U > UINTPTR_MAX - left_base ||
	    right_size - 1U > UINTPTR_MAX - right_base)
		return true;
	return left_base <= right_base + right_size - 1U &&
		right_base <= left_base + left_size - 1U;
}

bool payload_mm_authvar_media_buffer_disjoint(const void *buffer, size_t size)
{
	if (fake_provider_active) {
		fake_provider_violation = true;
		poisoned = true;
		cache_bound = false;
		return false;
	}
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (buffer == fmp_reject_media_span && size == fmp_reject_media_span_size)
		return false;
#endif
	return buffer && size;
}

bool payload_mm_authvar_media_provider_enter(
	struct payload_mm_authvar_media_provider_scope *scope)
{
	if (fake_provider_active) {
		fake_provider_violation = true;
		poisoned = true;
		return false;
	}
	fake_provider_active = true;
	*scope = (struct payload_mm_authvar_media_provider_scope) {
		.cookie = 3,
		.check = 4,
	};
	return true;
}

bool payload_mm_authvar_media_provider_leave(
	struct payload_mm_authvar_media_provider_scope *scope)
{
	if (!scope || !fake_provider_active || scope->cookie != 3 ||
	    scope->check != 4) {
		fake_provider_violation = true;
		poisoned = true;
		return false;
	}
	fake_provider_active = false;
	return true;
}

bool payload_mm_authvar_media_provider_violated(void)
{
	return fake_provider_violation;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_begin(
	uint64_t *generation, uint64_t *token)
{
	begin_count++;
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (fmp_mutate_caller & 1U) {
		fmp_callback_current->data[0] ^= 0x80U;
		fmp_mutate_caller &= ~1U;
	}
	if (fmp_mutate_caller & 2U) {
		fmp_callback_candidate->data[0] ^= 0x80U;
		fmp_mutate_caller &= ~2U;
	}
	if (fmp_reenter_on_begin) {
		fmp_reenter_on_begin = false;
		fmp_nested_status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL,
			&fmp_nested_output);
	}
	if (fmp_abort_on_begin) {
		fmp_abort_on_begin = false;
		payload_mm_authvar_fmp_state_activation_abort();
	}
	if (fmp_initialize_reenter_session == begin_count) {
		fmp_initialize_reenter_session = 0;
		fmp_nested_status = payload_mm_authvar_fmp_state_initialize(
			&fmp_nested_output);
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (native_mutate_on_begin) {
		native_external_request->attributes = 0U;
		native_external_request->vendor_guid[0] ^= 0xffU;
		native_external_name[0] ^= 0x20U;
		native_external_data[0] ^= 0xffU;
		native_mutate_on_begin = false;
	}
#endif
	if (reenter_on_begin) {
		reenter_on_begin = false;
		nested_status = payload_mm_authvar_executor_recover();
	}
	if (fault(TEST_CALLBACK_BEGIN))
		return generic_fault_result;
	*generation = 1;
	*token = 2;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_read(
	uint64_t generation, uint64_t token, uint32_t offset, void *buffer,
	size_t size)
{
	read_count++;
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	if (candidate_media_fault_armed &&
	    candidate_media_fault == CANDIDATE_MEDIA_FAULT_READ) {
		candidate_media_fault_armed = false;
		return candidate_media_fault_result;
	}
#endif
	if (alternate_recovery_plans && start_alternate_snapshot && !offset &&
	    size == BLOCK_SIZE) {
		make_clean();
		if (alternate_snapshot_count++ & 1U) {
			make_write(PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
			memcpy(spare(), media, BLOCK_SIZE);
		} else {
			make_write(PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
				PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		}
		start_alternate_snapshot = false;
	}
	if (fault(TEST_CALLBACK_READ) || generation != 1 || token != 2 ||
	    offset > MEDIA_SIZE || size > MEDIA_SIZE - offset)
		return generic_fault_result;
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (native_collision_on_read && !offset && size == BLOCK_SIZE &&
	    program_count > native_collision_program_count &&
	    ++native_collision_primary_reads == 2U) {
		native_inject_collision();
		native_collision_on_read = false;
	}
#endif
	if (offset == 3U * BLOCK_SIZE)
		tail_read_count++;
	if (fake_erased_reads && offset >= 2U * BLOCK_SIZE) {
		memset(buffer, 0xff, size);
		fake_erased_reads--;
	} else {
		memcpy(buffer, media + offset, size);
	}
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	if (mor_take_observed && mor_seal_mutate_on_read) {
		assert(payload_mm_authvar_executor_test_mutate_mor_seal(
			mor_seal_mutation));
		mor_seal_mutate_on_read = false;
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	if (recovery_image_to_mutate &&
	    offset == recovery_image_mutation_read_offset &&
	    size == recovery_image_mutation_read_size) {
		if (recovery_image_mutation_matches_to_skip) {
			recovery_image_mutation_matches_to_skip--;
		} else {
			assert(recovery_image_to_mutate_size);
			recovery_image_to_mutate[
				recovery_image_to_mutate_size - 1U] ^= 1U;
			recovery_image_to_mutate = NULL;
			recovery_image_to_mutate_size = 0U;
		}
	}
#endif
	if (corrupt_marker_expected_read && size == 1U &&
	    offset == FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U) {
		((uint8_t *)buffer)[0] = PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY;
		corrupt_marker_expected_read = false;
	}
	if (erased_spare_readback_pending && offset == 2U * BLOCK_SIZE) {
		((uint8_t *)buffer)[0] = 0x7fU;
		erased_spare_readback_pending = false;
	}
	if (corrupt_replay_spare_readback && offset == 2U * BLOCK_SIZE &&
	    ++replay_spare_read_count == 2U)
		((uint8_t *)buffer)[0] ^= 1U;
	if (corrupt_replay_primary_readback && !offset &&
	    ++replay_primary_read_count == 2U)
		((uint8_t *)buffer)[0] ^= 1U;
	if (corrupt_after_final_verify && tail_read_count == 2U &&
	    offset == 3U * BLOCK_SIZE) {
		media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
			PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + 4U] &= 0xfeU;
		corrupt_after_final_verify = false;
	}
	if (stage_spare_after_final_verify && tail_read_count == 2U &&
	    offset == 3U * BLOCK_SIZE) {
		memcpy(media + 2U * BLOCK_SIZE, media, BLOCK_SIZE);
		stage_spare_after_final_verify = false;
	}
	if (mutate_control_on_read) {
		struct payload_mm_authvar_contract wanted;

		assert(payload_mm_authvar_authority_snapshot(&wanted));
		for (size_t i = 0; i <= sizeof(arena) - sizeof(wanted); i++) {
			if (!memcmp(arena + i, &wanted, sizeof(wanted))) {
				struct payload_mm_authvar_contract *live =
					(void *)(arena + i);

				live->block_size ^= 1U;
				mutate_control_on_read = false;
				break;
			}
		}
		assert(!mutate_control_on_read);
	}
	if (mutate_record_on_read) {
		for (size_t i = 0; i + 2U < sizeof(arena); i++) {
			if (arena[i] == 0xaaU && arena[i + 1U] == 0x55U &&
			    arena[i + 2U] == 0xffU) {
				arena[i + 2U] = 0x7fU;
				mutate_record_on_read = false;
				break;
			}
		}
	}
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (fmp_tandem_phase == 2U && fmp_record_media_offset >= offset &&
	    fmp_record_media_offset < offset + size) {
		size_t position = fmp_record_media_offset - offset;

		media[fmp_record_media_offset] ^= 1U;
		((uint8_t *)buffer)[position] ^= 1U;
		payload_mm_authvar_fmp_test_mutate_record(0);
		fmp_tandem_phase = 0;
	}
#endif
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_program(
	uint64_t generation, uint64_t token, uint32_t offset, const void *buffer,
	size_t size)
{
	const uint8_t *wanted = buffer;
	const uint32_t direct_record_state = FV_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U;

	program_count++;
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (fmp_require_first_persistence_binding) {
		assert(payload_mm_authvar_fmp_test_record_binding_active());
		fmp_require_first_persistence_binding = false;
	}
	if (fmp_tandem_end || fmp_tandem_phase) {
		size_t buffer_offset;
		bool separate_image;

		if (payload_mm_authvar_fmp_test_record_target(buffer, size,
			fmp_tandem_tail, fmp_tandem_padding, &buffer_offset,
			&fmp_record_canonical_offset, &separate_image)) {
			fmp_record_media_offset = offset + (uint32_t)buffer_offset;
			fmp_record_separate_image = separate_image;
		}
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	assert(mor_take_observed);
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	if (candidate_media_fault_armed &&
	    candidate_media_fault == CANDIDATE_MEDIA_FAULT_PROGRAM) {
		candidate_media_fault_armed = false;
		return candidate_media_fault_result;
	}
	if (mutate_candidate_header_on_program && offset == 2U * BLOCK_SIZE &&
	    size == BLOCK_SIZE) {
		assert(candidate_image_mutation_offset < size &&
			candidate_image_mutation_bit < 8U);
		((uint8_t *)buffer)[candidate_image_mutation_offset] ^=
			(uint8_t)(1U << candidate_image_mutation_bit);
		mutate_candidate_header_on_program = false;
	}
	if (mutate_candidate_source_on_program && offset == BLOCK_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE + 1U) {
		media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE] ^= 1U;
		mutate_candidate_source_on_program = false;
	}
#endif
	if (program_count <= ARRAY_SIZE(program_trace))
		program_trace[program_count - 1U] = (struct write_trace_entry) {
			.offset = offset,
			.size = (uint32_t)size,
			.first = size ? wanted[0] : 0,
		};
	if (size > 1U && offset <= direct_record_state &&
	    size > direct_record_state - offset)
		record_state_in_body = true;
	if (size == 1U && offset == direct_record_state)
		record_state_marker_programmed = true;
	if (size == 1U && offset == BLOCK_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE)
		ftw_spare_marker_programmed = true;
	if (size == 1U &&
	    offset == BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE &&
	    wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED)
		ftw_header_allocated_marker_programmed = true;
	if (size == 1U && offset == BLOCK_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE &&
	    wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE)
		ftw_destination_marker_programmed = true;
	if (size == 1U && wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID &&
	    offset == 2U * BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET)
		workspace_spare_marker_programmed = true;
	if (size == 1U && wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID &&
	    offset == BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET)
		workspace_working_marker_programmed = true;
	if (size == 1U &&
	    offset == BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET &&
	    wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_WORK_INVALID)
		workspace_old_invalid_marker_programmed = true;
	if (size == 1U &&
	    offset == BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE &&
	    wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE)
		restore_queue_committed = true;
	if (fault(TEST_CALLBACK_PROGRAM) || generation != 1 || token != 2 ||
	    offset > MEDIA_SIZE || size > MEDIA_SIZE - offset)
		return generic_fault_result;
	for (size_t i = 0; i < size; i++) {
		if ((media[offset + i] & wanted[i]) != wanted[i])
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		media[offset + i] = wanted[i];
	}
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (fmp_tandem_phase == 1U && fmp_record_media_offset >= offset &&
	    fmp_record_media_offset < offset + size) {
		size_t position = fmp_record_media_offset - offset;

		media[fmp_record_media_offset] ^= 1U;
		if (fmp_record_separate_image)
			((uint8_t *)buffer)[position] ^= 1U;
		payload_mm_authvar_fmp_test_mutate_record(fmp_record_canonical_offset);
		fmp_tandem_attack_program_count = program_count;
		fmp_tandem_phase = 0;
	}
#endif
	if (alternate_recovery_plans && size == 1U &&
	    offset == BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE &&
	    wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE)
		start_alternate_snapshot = true;
	if (corrupt_before_final_verify && size == 1U &&
	    offset == direct_record_state) {
		media[BLOCK_SIZE - 1U] = 0xfeU;
		corrupt_before_final_verify = false;
	}
	if (corrupt_body_program && size > 1U &&
	    offset <= direct_record_state && direct_record_state <= offset + size) {
		for (size_t i = 0; i < size; i++) {
			if (media[offset + i]) {
				media[offset + i] &= (uint8_t)(media[offset + i] - 1U);
				corrupt_body_program = false;
				break;
			}
		}
	}
	if (corrupt_spare_suffix && offset == 2U * BLOCK_SIZE &&
	    size == BLOCK_SIZE) {
		media[3U * BLOCK_SIZE] = 0x7fU;
		corrupt_spare_suffix = false;
	}
	if (corrupt_ftw_spare_body && offset == 2U * BLOCK_SIZE && size) {
		media[offset] ^= 1U;
		corrupt_ftw_spare_body = false;
	}
	if (corrupt_workspace_spare_body &&
	    offset == 2U * BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET + 1U) {
		media[offset] ^= 1U;
		corrupt_workspace_spare_body = false;
	}
	if (corrupt_workspace_spare_suffix &&
	    offset == 2U * BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET + 1U) {
		media[3U * BLOCK_SIZE] = 0x7fU;
		corrupt_workspace_spare_suffix = false;
	}
	if (corrupt_workspace_working_body &&
	    offset == BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET + 1U) {
		media[offset] ^= 1U;
		corrupt_workspace_working_body = false;
	}
	if (corrupt_queue_header_body &&
	    offset == BLOCK_SIZE + PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE + 1U) {
		media[offset] ^= 1U;
		corrupt_queue_header_body = false;
	}
	if (corrupt_queue_record_body && offset == BLOCK_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE +
	    PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U) {
		media[offset] ^= 1U;
		corrupt_queue_record_body = false;
	}
	if (corrupt_primary_body && !offset && size == BLOCK_SIZE) {
		media[offset] ^= 1U;
		corrupt_primary_body = false;
	}
	if (mutate_on_program) {
		arena[500] ^= 1U;
		mutate_on_program = false;
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_erase(
	uint64_t generation, uint64_t token, uint32_t offset, size_t size)
{
	erase_count++;
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (fmp_require_first_persistence_binding) {
		assert(payload_mm_authvar_fmp_test_record_binding_active());
		fmp_require_first_persistence_binding = false;
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	assert(mor_take_observed);
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	if (candidate_media_fault_armed &&
	    candidate_media_fault == CANDIDATE_MEDIA_FAULT_ERASE) {
		candidate_media_fault_armed = false;
		return candidate_media_fault_result;
	}
#endif
	if (erase_count <= ARRAY_SIZE(erase_trace))
		erase_trace[erase_count - 1U] = (struct write_trace_entry) {
			.offset = offset,
			.size = (uint32_t)size,
		};
	if (fault(TEST_CALLBACK_ERASE) || generation != 1 || token != 2 ||
	    size != BLOCK_SIZE ||
	    offset % BLOCK_SIZE || offset > MEDIA_SIZE ||
	    size > MEDIA_SIZE - offset)
		return generic_fault_result;
	if (!erase_noop)
		memset(media + offset, 0xff, size);
	else if (offset + size == MEDIA_SIZE)
		fake_erased_reads = (MEDIA_SIZE - 2U * BLOCK_SIZE) / BLOCK_SIZE;
	if (!offset)
		primary_erase_count++;
	if (check_workspace_invalidate_order && offset == BLOCK_SIZE &&
	    !workspace_old_invalid_marker_programmed)
		workspace_erased_before_invalidate = true;
	if (check_restore_queue_order && offset == 2U * BLOCK_SIZE &&
	    !restore_queue_committed)
		spare_erased_before_restore_queue = true;
	if (corrupt_erased_spare_readback && offset == 2U * BLOCK_SIZE)
		erased_spare_readback_pending = true;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_end(
	uint64_t generation, uint64_t token)
{
	end_count++;
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	if (fmp_control_mutate_at_end) {
		payload_mm_authvar_fmp_test_mutate_control(
			fmp_control_mutate_at_end - 1U);
		fmp_control_mutate_at_end = 0U;
	}
	if ((fmp_mutate_caller & 4U) && (!fmp_mutate_caller_session ||
	    fmp_mutate_caller_session == begin_count)) {
		fmp_callback_published->data[0] ^= 0x80U;
		fmp_mutate_caller &= ~4U;
		fmp_mutate_caller_session = 0;
	}
	if (fmp_mutate_at_end && (!fmp_mutate_workspace_session ||
	    fmp_mutate_workspace_session == begin_count)) {
		payload_mm_authvar_fmp_test_mutate_workspace(fmp_mutate_at_end - 1U);
		fmp_mutate_at_end = 0;
		fmp_mutate_workspace_session = 0;
	}
	if (fmp_tandem_end && fmp_record_media_offset) {
		media[fmp_record_media_offset] ^= 1U;
		payload_mm_authvar_fmp_test_mutate_record(fmp_record_canonical_offset);
		fmp_tandem_end = false;
	}
#endif
	if (generation != 1 || token != 2 || fault(TEST_CALLBACK_END) || poisoned ||
	    fail_end)
		return generic_fault_result;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_fail_closed(
	uint64_t generation, uint64_t token)
{
	assert((generation == 1 && token == 2) || (!generation && !token));
	fail_closed_count++;
	poisoned = true;
	cache_bound = false;
	return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

void payload_mm_authvar_media_cache_bind(uint64_t generation, uint64_t token)
{
	assert(generation == 1 && token == 2 && !poisoned);
	cache_bind_count++;
	cache_bound = true;
}

void payload_mm_authvar_media_cache_invalidate(void)
{
	cache_bound = false;
}

uint64_t payload_mm_authvar_media_result_status(
	enum payload_mm_authvar_media_result result)
{
	switch (result) {
	case PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS:
		return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	case PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	case PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED:
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	case PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR:
	default:
		return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
	}
}
#else
extern char _start[];
extern char _end[];

struct real_context {
	uint32_t marker;
};

static struct real_context real_context = { .marker = 0x5245414cU };
static struct payload_mm_authvar_media_port real_port;
static void *communication;

static void real_media_prepare(void)
{
	media = mmap(NULL, MEDIA_SIZE, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	assert(media != MAP_FAILED);
}

static bool real_yes(void *context)
{
	(void)context;
	return true;
}

static bool real_reserve(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	return base && size == BLOCK_SIZE;
}

static bool real_own_store(void *context, uint64_t offset, uint64_t size)
{
	(void)context;
	return offset == 0x600000U && size == REGION_SIZE;
}

static bool real_protected(void *context, const void *storage, size_t size)
{
	(void)context;
	return storage && size;
}

static enum payload_mm_authvar_media_result real_begin(const void *context,
	uint64_t *generation)
{
	const struct real_context *real = context;

	begin_count++;
	if (real->marker != 0x5245414cU || fault(TEST_CALLBACK_BEGIN))
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	*generation = 1;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result real_read(const void *context,
	uint32_t offset, void *buffer, size_t size, size_t *completed)
{
	const struct real_context *real = context;

	read_count++;
	if (real->marker != 0x5245414cU || fault(TEST_CALLBACK_READ) ||
	    offset > MEDIA_SIZE ||
	    size > MEDIA_SIZE - offset)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	memcpy(buffer, media + offset, size);
	*completed = size;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result real_program(const void *context,
	uint32_t offset, const void *buffer, size_t size)
{
	const struct real_context *real = context;
	const uint8_t *wanted = buffer;

	program_count++;
	if (real->marker != 0x5245414cU || fault(TEST_CALLBACK_PROGRAM) ||
	    offset > MEDIA_SIZE ||
	    size > MEDIA_SIZE - offset)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	for (size_t i = 0; i < size; i++)
		media[offset + i] &= wanted[i];
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result real_erase(const void *context,
	uint32_t offset, size_t size)
{
	const struct real_context *real = context;

	erase_count++;
	if (real->marker != 0x5245414cU || fault(TEST_CALLBACK_ERASE) ||
	    offset > MEDIA_SIZE ||
	    size > MEDIA_SIZE - offset)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	memset(media + offset, 0xff, size);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result real_sync(const void *context)
{
	const struct real_context *real = context;

	return real->marker == 0x5245414cU && !fault(TEST_CALLBACK_SYNC) ?
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS :
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

static enum payload_mm_authvar_media_result real_end(const void *context)
{
	const struct real_context *real = context;

	end_count++;
	return real->marker == 0x5245414cU && !fault(TEST_CALLBACK_END) ?
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS :
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

static void real_stack_install(void)
{
	struct payload_mm_authvar_platform platform;
	struct payload_mm_authvar_contract contract;

	communication = mmap(NULL, BLOCK_SIZE, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	assert(communication != MAP_FAILED);
	memset(&platform, 0, sizeof(platform));
	platform.smm_address_bits = 64;
	platform.generation = 7;
	platform.smram.base = (uintptr_t)_start;
	platform.smram.size = UINTPTR_MAX - (uintptr_t)_start;
	platform.communication.base = (uintptr_t)communication;
	platform.communication.size = BLOCK_SIZE;
	platform.boot_media_size = 0x1000000U;
	platform.store_offset = 0x600000U;
	platform.store_size = REGION_SIZE;
	platform.block_size = BLOCK_SIZE;
	platform.erase_size = BLOCK_SIZE;
	platform.smm_entry_owned = real_yes;
	platform.spi_writes_restricted_to_smm = real_yes;
	platform.raw_flash_transport_absent = real_yes;
	platform.communication_region_reserved = real_reserve;
	platform.store_region_owned_by_smm = real_own_store;
	assert(payload_mm_authvar_contract_build(&contract, &platform) == CB_SUCCESS);
	assert(payload_mm_authvar_authority_install(&contract, real_protected, NULL) ==
		CB_SUCCESS);
	memset(&real_port, 0, sizeof(real_port));
	real_port.revision = PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION;
	real_port.size = sizeof(real_port);
	real_port.begin = real_begin;
	real_port.read = real_read;
	real_port.program = real_program;
	real_port.erase = real_erase;
	real_port.sync = real_sync;
	real_port.end = real_end;
	real_port.context = &real_context;
	real_port.context_size = sizeof(real_context);
	assert(payload_mm_authvar_media_install(&real_port) == CB_SUCCESS);
}
#endif

#ifdef EXECUTOR_SOURCE_INCLUDE
#include EXECUTOR_SOURCE_INCLUDE
#endif

static void coordinator_install_executor(void)
{
	static struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};
	size_t required_size = 0;

#ifdef EXECUTOR_REAL_MEDIA
	real_stack_install();
#endif

	assert(payload_mm_authvar_executor_required_size(&limits, &required_size) ==
		CB_SUCCESS);
	assert(required_size && required_size <= sizeof(arena));
	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_SUCCESS);
}

static void install(void)
{
	coordinator_install_executor();
#if !CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	assert(test_policy_install() == CB_SUCCESS);
#endif
}

#ifdef EXECUTOR_SOURCE_INCLUDE
static void reject_illegal_nor_marker(void)
{
	struct executor_session *state;
	uint32_t offset = FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U;

	install();
	state = session();
	memset(state, 0, sizeof(*state));
	assert(media_begin(state) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	media[offset] = PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY;
	assert(marker(state, offset,
		PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY,
		PAYLOAD_MM_AUTHVAR_STATE_ERASED) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
	assert(state->invariant_failure && program_count == 0U);
	assert(media_end(state) == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
}
#endif

static void reject_misaligned_arena(void)
{
	struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 4096,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};

	assert(payload_mm_authvar_executor_install(arena + 1, sizeof(arena) - 1,
		&limits) == CB_ERR);
}

static void reject_install(const char *test_case)
{
	struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};
	void *candidate_arena = arena;
	size_t candidate_size = sizeof(arena);
	const struct payload_mm_authvar_executor_limits *candidate_limits = &limits;

	if (!strcmp(test_case, "null-arena"))
		candidate_arena = NULL;
	else if (!strcmp(test_case, "zero-arena"))
		candidate_size = 0;
	else if (!strcmp(test_case, "null-limits"))
		candidate_limits = NULL;
	else if (!strcmp(test_case, "undersize-arena"))
		candidate_size = 64;
	else if (!strcmp(test_case, "record-limits"))
		limits.maximum_record_size = 2048;
	else if (!strcmp(test_case, "geometry-limits"))
		limits.maximum_store_size = REGION_SIZE - 1U;
	else if (!strcmp(test_case, "overflow-layout"))
		limits.maximum_records = UINT32_MAX;
	else
		assert(false);
	assert(payload_mm_authvar_executor_install(candidate_arena, candidate_size,
		candidate_limits) == CB_ERR);
}

static void reject_second_install(void)
{
	static const struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};

	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_SUCCESS);
	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_ERR);
}

static void reject_invalid_geometry(void)
{
	static const struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};

	authority_store_size = REGION_SIZE - 1U;
	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_ERR);
}

enum write_case {
	WRITE_ADD,
	WRITE_REPLACE,
	WRITE_RECLAIM,
	WRITE_DELETE,
	WRITE_EMPTY_APPEND_EXISTING,
	WRITE_EMPTY_APPEND_ABSENT,
	WRITE_MUTATE,
	WRITE_RECORD_STATE_MUTATE,
	WRITE_BODY_READBACK_MUTATE,
	WRITE_SPARE_SUFFIX_MUTATE,
	WRITE_SPARE_BODY_MUTATE,
	WRITE_QUEUE_HEADER_MUTATE,
	WRITE_QUEUE_RECORD_MUTATE,
	WRITE_PRIMARY_BODY_MUTATE,
	WRITE_FINAL_FRESH_READ_MUTATE,
	WRITE_FINAL_COMPARE_MUTATE,
	WRITE_FINAL_FTW_ACTION_MUTATE,
	WRITE_MARKER_EXPECTED_MUTATE,
	WRITE_RECLAIM_SCAN_REJECT,
};

static void direct_write(enum write_case test_case)
{
	static const uint8_t name[] = { 'A', 0, 0, 0 };
	static uint8_t data[] = { 1, 2, 3, 4 };
	struct payload_mm_authvar_record_source source = {
		.name = name,
		.name_size = sizeof(name),
		.data = data,
		.data_size = sizeof(data),
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
	};
	bool force_reclaim = test_case == WRITE_RECLAIM ||
			test_case == WRITE_RECLAIM_SCAN_REJECT ||
			test_case == WRITE_SPARE_SUFFIX_MUTATE ||
			test_case == WRITE_SPARE_BODY_MUTATE ||
			test_case == WRITE_QUEUE_HEADER_MUTATE ||
			test_case == WRITE_QUEUE_RECORD_MUTATE ||
			test_case == WRITE_PRIMARY_BODY_MUTATE;
	uint64_t status;
	char message[96];

	memcpy(source.vendor_guid, caller_guid, sizeof(caller_guid));
	install();
	if (test_case == WRITE_EMPTY_APPEND_ABSENT) {
		source.data = NULL;
		source.data_size = 0;
		source.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	}
	mutate_on_program = test_case == WRITE_MUTATE;
	mutate_record_on_read = test_case == WRITE_RECORD_STATE_MUTATE;
	corrupt_body_program = test_case == WRITE_BODY_READBACK_MUTATE;
	corrupt_spare_suffix = test_case == WRITE_SPARE_SUFFIX_MUTATE;
	corrupt_ftw_spare_body = test_case == WRITE_SPARE_BODY_MUTATE;
	corrupt_queue_header_body = test_case == WRITE_QUEUE_HEADER_MUTATE;
	corrupt_queue_record_body = test_case == WRITE_QUEUE_RECORD_MUTATE;
	corrupt_primary_body = test_case == WRITE_PRIMARY_BODY_MUTATE;
	corrupt_after_final_verify = test_case == WRITE_FINAL_FRESH_READ_MUTATE;
	corrupt_before_final_verify = test_case == WRITE_FINAL_COMPARE_MUTATE;
	stage_spare_after_final_verify = test_case == WRITE_FINAL_FTW_ACTION_MUTATE;
	corrupt_marker_expected_read = test_case == WRITE_MARKER_EXPECTED_MUTATE;
	status = test_policy_apply(&source);
	if (test_case == WRITE_MARKER_EXPECTED_MUTATE) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && !record_state_marker_programmed && end_count == 1);
		return;
	}
	if (test_case == WRITE_FINAL_FTW_ACTION_MUTATE) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && !cache_bound && end_count == 1);
		return;
	}
	if (test_case == WRITE_FINAL_COMPARE_MUTATE) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && !cache_bound && !cache_bind_count && end_count == 1);
		return;
	}
	if (test_case == WRITE_FINAL_FRESH_READ_MUTATE) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && !cache_bound && end_count == 1);
		return;
	}
	if (test_case == WRITE_BODY_READBACK_MUTATE) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && !record_state_marker_programmed && end_count == 1);
		return;
	}
	if (test_case == WRITE_RECORD_STATE_MUTATE) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && program_count == 0 && end_count == 1);
		return;
	}
	if (test_case == WRITE_MUTATE) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && begin_count == 1 && end_count == 1 && !cache_bound);
		return;
	}
	if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
		int length = snprintf(message, sizeof(message),
			"direct add status: 0x%llx\n", (unsigned long long)status);

		if (length > 0)
			(void)write(2, message, (unsigned long)length);
	}
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	if (test_case == WRITE_RECLAIM || test_case == WRITE_RECLAIM_SCAN_REJECT ||
	    test_case == WRITE_REPLACE ||
	    test_case == WRITE_SPARE_SUFFIX_MUTATE ||
	    test_case == WRITE_SPARE_BODY_MUTATE ||
	    test_case == WRITE_QUEUE_HEADER_MUTATE ||
	    test_case == WRITE_QUEUE_RECORD_MUTATE ||
	    test_case == WRITE_PRIMARY_BODY_MUTATE) {
		data[0] = 9U;
		/* A torn, uncommitted tail is unavailable for append until reclaim. */
		if (force_reclaim)
			media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
				PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + sizeof(name) + sizeof(data)] =
				0xaaU;
		if (test_case == WRITE_RECLAIM_SCAN_REJECT) {
			reject_precommit_scan = true;
			wrapped_scan_count = 0;
			precommit_program_count = program_count;
		}
		status = test_policy_apply(&source);
		if (test_case == WRITE_RECLAIM_SCAN_REJECT) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
			assert(poisoned && program_count == precommit_program_count &&
				end_count == 2);
			return;
		}
		if (test_case == WRITE_SPARE_SUFFIX_MUTATE ||
		    test_case == WRITE_SPARE_BODY_MUTATE ||
		    test_case == WRITE_QUEUE_HEADER_MUTATE ||
		    test_case == WRITE_QUEUE_RECORD_MUTATE ||
		    test_case == WRITE_PRIMARY_BODY_MUTATE) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
			assert(poisoned && end_count == 2);
			if (test_case == WRITE_QUEUE_HEADER_MUTATE)
				assert(!ftw_header_allocated_marker_programmed);
			else if (test_case == WRITE_QUEUE_RECORD_MUTATE)
				assert(!ftw_spare_marker_programmed);
			else if (test_case == WRITE_PRIMARY_BODY_MUTATE)
				assert(!ftw_destination_marker_programmed);
			else
				assert(!ftw_spare_marker_programmed);
			return;
		}
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			int length = snprintf(message, sizeof(message),
				"reclaim status: 0x%llx\n",
				(unsigned long long)status);

			if (length > 0)
				(void)write(2, message, (unsigned long)length);
		}
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	} else if (test_case == WRITE_DELETE) {
		source.data = NULL;
		source.data_size = 0;
		status = test_policy_apply(&source);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	} else if (test_case == WRITE_EMPTY_APPEND_EXISTING) {
		source.data = NULL;
		source.data_size = 0;
		source.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
		status = test_policy_apply(&source);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	}
	assert(begin_count == ((test_case == WRITE_RECLAIM ||
		test_case == WRITE_REPLACE || test_case == WRITE_DELETE ||
		test_case == WRITE_EMPTY_APPEND_EXISTING) ? 2U : 1U) &&
		end_count == begin_count && cache_ok() && !poisoned);
	if (test_case == WRITE_ADD)
		assert(!record_state_in_body);
}

static void expect_success(void)
{
	struct payload_mm_authvar_ftw_plan plan;

	install();
	assert(payload_mm_authvar_executor_recover() ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(begin_count == 1 && end_count == 1 && cache_ok() && !poisoned);
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE, &plan) ==
		CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
}

#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
static void fmp_prepare_modes(void);
static bool erased(const uint8_t *bytes, size_t size);

static bool fmp_protected(void *unused, const void *storage, size_t size)
{
	(void)unused;
	return storage && size;
}

static enum cb_err fmp_activation_backend_read(const void *unused,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	struct payload_mm_fmp_owner_record *record)
{
	struct payload_mm_fmp_state_identity expected;
	uint64_t status;

	(void)unused;
	if (key != PAYLOAD_MM_FMP_STATE_KEY_STATE ||
	    payload_mm_fmp_owner_authvar_identity(key, &expected) != CB_SUCCESS ||
	    memcmp(identity, &expected, sizeof(expected)))
		return CB_ERR;
	status = payload_mm_authvar_fmp_state_activation_read(record);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	fmp_activation_read_count++;
	record->sequence = 1U;
	assert(payload_mm_fmp_owner_observed_record_valid(key, record));
	return CB_SUCCESS;
}

static enum cb_err fmp_activation_backend_commit(const void *unused,
	const struct payload_mm_fmp_state_identity *identity, uint32_t key,
	const struct payload_mm_fmp_owner_record *current,
	const struct payload_mm_fmp_owner_record *candidate)
{
	(void)unused;
	(void)identity;
	(void)key;
	(void)current;
	(void)candidate;
	return CB_ERR;
}

static void fmp_install_checked_owner(
	const struct payload_mm_fmp_owner_record *initial)
{
	const uint8_t context = 0x5aU;
	const struct payload_mm_fmp_owner_backend backend = {
		.revision = PAYLOAD_MM_FMP_OWNER_REVISION,
		.size = sizeof(backend),
		.read = fmp_activation_backend_read,
		.commit = fmp_activation_backend_commit,
		.context = &context,
		.context_size = sizeof(context),
	};
	assert(payload_mm_fmp_owner_install_checked(&backend, fmp_protected, NULL,
		PAYLOAD_MM_FMP_STATE_KEY_STATE, initial) == CB_SUCCESS);
}

static void fmp_install_state_policy(uint32_t trusted_lowest_version)
{
	struct payload_mm_fmp_state_policy policy = {
		.revision = PAYLOAD_MM_FMP_STATE_POLICY_REVISION,
		.size = sizeof(policy),
		.namespace_guid.b = { 1U },
		.trusted_lowest_version = trusted_lowest_version,
	};

	assert(payload_mm_fmp_state_policy_install(&policy, fmp_protected, NULL) ==
		CB_SUCCESS);
}

static const struct payload_mm_authvar_store_entry *fmp_store_entry(
	struct payload_mm_authvar_store_index *index,
	struct payload_mm_authvar_store_entry *entries, size_t entry_count)
{
	struct payload_mm_fmp_state_identity identity;
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = (uint32_t)entry_count,
	};

	assert(payload_mm_fmp_owner_authvar_identity(
		PAYLOAD_MM_FMP_STATE_KEY_STATE, &identity) == CB_SUCCESS);
	memset(index, 0, sizeof(*index));
	index->entries = entries;
	index->entry_capacity = (uint32_t)entry_count;
	assert(payload_mm_authvar_store_scan(index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	return payload_mm_authvar_store_find(index, identity.namespace_guid.b,
		identity.variable_name, identity.variable_name_bytes);
}

static const struct payload_mm_authvar_store_entry *fmp_key_entry(uint32_t key,
	struct payload_mm_authvar_store_index *index,
	struct payload_mm_authvar_store_entry *entries, size_t entry_count)
{
	struct payload_mm_fmp_state_identity identity;

	(void)fmp_store_entry(index, entries, entry_count);
	assert(payload_mm_fmp_owner_authvar_identity(key, &identity) == CB_SUCCESS);
	return payload_mm_authvar_store_find(index, identity.namespace_guid.b,
		identity.variable_name, identity.variable_name_bytes);
}

static void fmp_dirty_tail(void)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;

	(void)fmp_store_entry(&index, entries, ARRAY_SIZE(entries));
	assert(index.used_size < index.store_size);
	media[FV_HEADER_SIZE + index.used_size] = 0xaaU;
}

enum fmp_stored_mutation {
	FMP_STORED_ATTRIBUTES,
	FMP_STORED_SIZE,
	FMP_STORED_DATA,
};

static void fmp_mutate_stored(enum fmp_stored_mutation mutation)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	const struct payload_mm_authvar_store_entry *entry =
		fmp_store_entry(&index, entries, ARRAY_SIZE(entries));
	uint8_t *record;

	assert(entry);
	record = media + FV_HEADER_SIZE + entry->record_offset;
	switch (mutation) {
	case FMP_STORED_ATTRIBUTES:
		put32(record + 4U, entry->attributes |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
		break;
	case FMP_STORED_SIZE:
		put32(record + 40U, entry->data_size - 1U);
		break;
	case FMP_STORED_DATA:
		media[FV_HEADER_SIZE + entry->data_offset] = 2U;
		break;
	}
}

static struct payload_mm_fmp_owner_record fmp_absent(uint64_t sequence)
{
	return (struct payload_mm_fmp_owner_record) { .sequence = sequence };
}

static struct payload_mm_fmp_owner_record fmp_present(uint64_t sequence)
{
	return (struct payload_mm_fmp_owner_record) {
		.sequence = sequence,
		.present = 1,
		.attributes = PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES,
		.data_size = PAYLOAD_MM_FMP_STATE_WIRE_SIZE,
	};
}

static void fmp_assert_version_record(
	const struct payload_mm_fmp_owner_record *record, uint32_t version)
{
	uint8_t expected[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = { 1U };

	memcpy(expected + 4U, &version, sizeof(version));
	assert(record->present && !record->sequence &&
		record->attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
		record->data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE &&
		!memcmp(record->data, expected, sizeof(expected)));
}

static void fmp_append_identity_value(
	const struct payload_mm_fmp_state_identity *identity,
	const void *data, size_t data_size, uint32_t attributes)
{
	static const uint8_t timestamp[16];
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_record_descriptor descriptor = {
		.attributes = attributes,
	};
	struct payload_mm_authvar_record_span span = {
		.data = data, .size = data_size,
	};
	uint32_t record_size;

	(void)fmp_store_entry(&index, entries, ARRAY_SIZE(entries));
	descriptor.name = identity->variable_name;
	descriptor.name_size = identity->variable_name_bytes;
	memcpy(descriptor.vendor_guid, identity->namespace_guid.b,
		sizeof(descriptor.vendor_guid));
	memcpy(descriptor.timestamp, timestamp, sizeof(timestamp));
	assert(payload_mm_authvar_record_encode(&descriptor,
		data_size ? &span : NULL, data_size ? 1U : 0U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
		media + FV_HEADER_SIZE + index.used_size,
		STORE_SIZE - index.used_size, &record_size));
	media[FV_HEADER_SIZE + index.used_size + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED;
}

static void fmp_append_value(uint32_t key, const void *data, size_t data_size,
	uint32_t attributes)
{
	struct payload_mm_fmp_state_identity identity;

	assert(payload_mm_fmp_owner_authvar_identity(key, &identity) == CB_SUCCESS);
	fmp_append_identity_value(&identity, data, data_size, attributes);
}

static void fmp_append_legacy(uint32_t key, uint32_t value)
{
	fmp_append_value(key, &value, sizeof(value),
		PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
}

static void fmp_hostile_case(const char *name)
{
	struct payload_mm_fmp_owner_record current = fmp_absent(1U);
	struct payload_mm_fmp_owner_record candidate = fmp_present(2U);
	struct payload_mm_fmp_owner_record output;
	struct payload_mm_fmp_owner_record sentinel;
	struct payload_mm_fmp_owner_record original_current = current;
	struct payload_mm_fmp_owner_record original_candidate = candidate;
	struct payload_mm_fmp_owner_record before;
	struct payload_mm_fmp_owner_record owner_slot;
	struct payload_mm_fmp_owner_record *current_pointer = &current;
	struct payload_mm_fmp_owner_record *candidate_pointer = &candidate;
	struct payload_mm_fmp_owner_record *published_pointer = &output;
	uint8_t misaligned_storage[sizeof(output) + _Alignof(typeof(output))];
	uint64_t status;

	memset(&sentinel, 0xa5, sizeof(sentinel));
	output = sentinel;
	fmp_prepare_modes();
	install();
	payload_mm_authvar_fmp_test_force_active();
	if (!strcmp(name, "fmp-runtime-read") ||
	    !strcmp(name, "fmp-runtime-compare")) {
		struct payload_mm_authvar_policy_request lifecycle = {
			.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
		};
		struct payload_mm_authvar_policy_result result;
		unsigned int begins;
		unsigned int ends;

		assert(payload_mm_authvar_policy_transaction(&lifecycle, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		begins = begin_count;
		ends = end_count;
		status = payload_mm_authvar_fmp_state_transaction(
			!strcmp(name, "fmp-runtime-read") ?
				PAYLOAD_MM_AUTHVAR_FMP_READ_STATE :
				PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE,
			!strcmp(name, "fmp-runtime-read") ? NULL : &current,
			!strcmp(name, "fmp-runtime-read") ? NULL : &candidate, &output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			begin_count == begins && end_count == ends && !poisoned);
		return;
	}
	if (!strcmp(name, "fmp-busy-reentry")) {
		fmp_nested_output = sentinel;
		fmp_nested_status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		fmp_reenter_on_begin = true;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			fmp_nested_status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&fmp_nested_output, &sentinel,
				sizeof(fmp_nested_output)) &&
			begin_count == 1U && end_count == 1U && !poisoned);
		return;
	}
	if (!strncmp(name, "fmp-callback-mutate-", 20U)) {
		struct payload_mm_fmp_owner_record admitted_current = current;
		struct payload_mm_fmp_owner_record admitted_candidate = candidate;
		struct payload_mm_fmp_owner_record expected = candidate;

		fmp_callback_current = &current;
		fmp_callback_candidate = &candidate;
		fmp_callback_published = &output;
		if (!strcmp(name + 20U, "current"))
			fmp_mutate_caller = 1U;
		else if (!strcmp(name + 20U, "candidate"))
			fmp_mutate_caller = 2U;
		else {
			assert(!strcmp(name + 20U, "published") ||
				!strcmp(name + 20U, "published-fail"));
			fmp_mutate_caller = 4U;
		}
		if (!strcmp(name + 20U, "published-fail"))
			fail_end = true;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		expected.sequence = 0U;
		if (!strcmp(name + 20U, "published-fail")) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
				!memcmp(&output, &sentinel, sizeof(output)) &&
				begin_count == 1U && end_count == 1U);
			return;
		}
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			!fmp_mutate_caller && !memcmp(&output, &expected, sizeof(output)) &&
			(!strcmp(name + 20U, "current") ?
				memcmp(&current, &admitted_current, sizeof(current)) != 0 :
				!memcmp(&current, &admitted_current, sizeof(current))) &&
			(!strcmp(name + 20U, "candidate") ?
				memcmp(&candidate, &admitted_candidate,
					sizeof(candidate)) != 0 :
				!memcmp(&candidate, &admitted_candidate,
					sizeof(candidate))) &&
			begin_count == 1U && end_count == 1U && !poisoned &&
			!fail_closed_count);
		return;
	}
	if (!strcmp(name, "fmp-admission-invalid-operation")) {
		status = payload_mm_authvar_fmp_state_transaction(
			(enum payload_mm_authvar_fmp_operation)99U, NULL, NULL, &output);
	} else if (!strcmp(name, "fmp-admission-read-current")) {
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, &current, NULL, &output);
	} else if (!strcmp(name, "fmp-admission-read-candidate")) {
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, &candidate, &output);
	} else if (!strcmp(name, "fmp-admission-null-published")) {
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			NULL);
	} else if (!strcmp(name, "fmp-admission-null-current")) {
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, NULL, &candidate,
			&output);
	} else if (!strcmp(name, "fmp-admission-null-candidate")) {
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, NULL, &output);
	} else if (!strncmp(name, "fmp-admission-misaligned-", 25U)) {
		struct payload_mm_fmp_owner_record *misaligned =
			(void *)(misaligned_storage + 1U);

		if (!strcmp(name + 25U, "published")) {
			memcpy(misaligned, &sentinel, sizeof(*misaligned));
			published_pointer = misaligned;
		} else if (!strcmp(name + 25U, "current")) {
			memcpy(misaligned, &current, sizeof(*misaligned));
			current_pointer = misaligned;
		} else {
			assert(!strcmp(name + 25U, "candidate"));
			memcpy(misaligned, &candidate, sizeof(*misaligned));
			candidate_pointer = misaligned;
		}
		memcpy(&before, misaligned, sizeof(before));
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, current_pointer,
			candidate_pointer, published_pointer);
		assert(!memcmp(misaligned, &before, sizeof(before)));
	} else if (!strncmp(name, "fmp-admission-unprotected-", 26U)) {
		owner_slot = !strcmp(name + 26U, "candidate") ? candidate : current;
		if (!strcmp(name + 26U, "published")) {
			owner_slot = sentinel;
			published_pointer = &owner_slot;
		} else if (!strcmp(name + 26U, "current")) {
			current_pointer = &owner_slot;
		} else {
			assert(!strcmp(name + 26U, "candidate"));
			candidate_pointer = &owner_slot;
		}
		before = owner_slot;
		fmp_reject_smram_span = &owner_slot;
		fmp_reject_smram_span_size = sizeof(owner_slot);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, current_pointer,
			candidate_pointer, published_pointer);
		assert(!memcmp(&owner_slot, &before, sizeof(owner_slot)));
	} else if (!strncmp(name, "fmp-admission-alias-", 20U)) {
		if (!strcmp(name + 20U, "current-candidate"))
			candidate_pointer = current_pointer;
		else if (!strcmp(name + 20U, "current-published"))
			published_pointer = current_pointer;
		else {
			assert(!strcmp(name + 20U, "candidate-published"));
			published_pointer = candidate_pointer;
		}
		before = *published_pointer;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, current_pointer,
			candidate_pointer, published_pointer);
		assert(!memcmp(published_pointer, &before, sizeof(before)));
	} else if (!strncmp(name, "fmp-admission-state-owner-", 26U) ||
		   !strncmp(name, "fmp-admission-state-authority-", 30U) ||
		   !strncmp(name, "fmp-admission-authvar-owner-", 28U)) {
		uint8_t owner_before[4096];
		size_t owner_size;
		const bool state = !strncmp(name, "fmp-admission-state-authority-", 30U);
		const bool authvar = !strncmp(name, "fmp-admission-authvar-owner-", 28U);
		const uint8_t *owner = state ?
			payload_mm_fmp_state_test_storage(&owner_size) : authvar ?
			payload_mm_fmp_owner_authvar_test_storage(&owner_size) :
			payload_mm_fmp_owner_test_storage(&owner_size);
		const char *role = name + (state ? 30U : authvar ? 28U : 26U);
		const bool partial = strstr(role, "-partial") != NULL;
		struct payload_mm_fmp_owner_record *blocked;

		assert(owner && owner_size <= sizeof(owner_before) && owner_size >= 24U);
		memcpy(owner_before, owner, owner_size);
		blocked = (void *)(partial ? owner + owner_size - 24U : owner);
		if (!strncmp(role, "published-", 10U))
			published_pointer = blocked;
		else if (!strncmp(role, "current-", 8U))
			current_pointer = blocked;
		else {
			assert(!strncmp(role, "candidate-", 10U));
			candidate_pointer = blocked;
		}
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, current_pointer,
			candidate_pointer, published_pointer);
		assert(!memcmp(owner, owner_before, owner_size));
	} else {
		const char *role;
		struct payload_mm_fmp_owner_record *blocked;

		if (!strncmp(name, "fmp-admission-arena-", 20U)) {
			role = name + 20U;
			blocked = (void *)arena;
		} else if (!strncmp(name, "fmp-admission-media-", 20U)) {
			role = name + 20U;
			blocked = (void *)ALIGN_UP((uintptr_t)media,
				_Alignof(struct payload_mm_fmp_owner_record));
			fmp_reject_media_span = blocked;
			fmp_reject_media_span_size = sizeof(*blocked);
		} else {
			assert(!strncmp(name, "fmp-admission-owner-", 20U));
			role = name + 20U;
			owner_slot = !strcmp(role, "published") ? sentinel :
				(!strcmp(role, "candidate") ? candidate : current);
			blocked = &owner_slot;
			payload_mm_fmp_test_owner_storage = blocked;
			payload_mm_fmp_test_owner_storage_size = sizeof(*blocked);
		}
		if (!strcmp(role, "published"))
			published_pointer = blocked;
		else if (!strcmp(role, "current"))
			current_pointer = blocked;
		else {
			assert(!strcmp(role, "candidate"));
			candidate_pointer = blocked;
		}
		memcpy(&before, blocked, sizeof(before));
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, current_pointer,
			candidate_pointer, published_pointer);
		assert(!memcmp(blocked, &before, sizeof(before)));
	}
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
		!begin_count && !end_count && !program_count && !erase_count &&
		!poisoned && !fail_closed_count &&
		!memcmp(&current, &original_current, sizeof(current)) &&
		!memcmp(&candidate, &original_candidate, sizeof(candidate)));
	if (published_pointer == &output)
		assert(!memcmp(&output, &sentinel, sizeof(output)));
}

static void fmp_state_case(const char *name)
{
	struct payload_mm_fmp_owner_record current = fmp_absent(1);
	struct payload_mm_fmp_owner_record candidate = fmp_present(2);
	struct payload_mm_fmp_owner_record output;
	struct payload_mm_fmp_owner_record sentinel;
	uint64_t status;

	memset(&sentinel, 0xa5, sizeof(sentinel));
	output = sentinel;
	if (!strcmp(name, "fmp-initialize-suffixed-isolation"))
		payload_mm_fmp_test_hardware_instance = 0x0123456789abcdefULL;
	fmp_prepare_modes();
	if (!strcmp(name, "fmp-trusted-lsv-floor") ||
	    !strncmp(name, "fmp-initialize-lsv-", 19U) ||
	    !strcmp(name, "fmp-monotonic-lsv"))
		fmp_install_state_policy(7U);
	if (!strcmp(name, "fmp-transient-read")) {
		candidate_media_fault = CANDIDATE_MEDIA_FAULT_READ;
		candidate_media_fault_result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		candidate_media_fault_armed = true;
	}
	if (!strcmp(name, "fmp-required-size-minus-one")) {
		const struct payload_mm_authvar_executor_limits limits = {
			.maximum_store_size = REGION_SIZE,
			.maximum_name_size = 128U,
			.maximum_data_size = 2048U,
			.maximum_record_size = 8192U,
			.maximum_records = 64U,
		};
		size_t required_size;

		assert(payload_mm_authvar_executor_required_size(&limits,
			&required_size) == CB_SUCCESS && required_size > 1U &&
			required_size <= sizeof(arena));
		assert(payload_mm_authvar_executor_install(arena, required_size - 1U,
			&limits) == CB_ERR);
		return;
	}
	install();
	if (!strcmp(name, "fmp-phase-activation") ||
	    !strcmp(name, "fmp-phase-mutate-live") ||
	    !strcmp(name, "fmp-phase-mutate-sealed") ||
	    !strcmp(name, "fmp-phase-abort") ||
	    !strcmp(name, "fmp-phase-abort-policy-mutate") ||
	    !strcmp(name, "fmp-phase-abort-busy")) {
		struct payload_mm_fmp_owner_record checked = sentinel;
		struct payload_mm_fmp_owner_record rejected = sentinel;
		unsigned int begins;
		unsigned int programs;

		fmp_install_state_policy(0U);
		assert(payload_mm_authvar_fmp_test_phase() == 0U);
		assert(payload_mm_authvar_fmp_state_reconciliation_retryable());
		assert(payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &rejected) ==
			PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) && !begin_count);
		assert(payload_mm_authvar_fmp_state_initialize(&output) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			payload_mm_authvar_fmp_test_phase() == 1U);
		assert(!payload_mm_authvar_fmp_state_reconciliation_retryable());
		begins = begin_count;
		programs = program_count;
		rejected = sentinel;
		assert(payload_mm_authvar_fmp_state_initialize(&rejected) ==
			PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
			begin_count == begins && program_count == programs);
		if (!strcmp(name, "fmp-phase-mutate-live") ||
		    !strcmp(name, "fmp-phase-mutate-sealed")) {
			payload_mm_authvar_fmp_test_mutate_phase(
				!strcmp(name, "fmp-phase-mutate-sealed"));
			assert(!payload_mm_authvar_fmp_state_reconciliation_retryable() &&
				poisoned &&
				!payload_mm_authvar_fmp_test_installed() &&
				payload_mm_authvar_fmp_test_phase() == 3U &&
				fail_closed_count == 1U);
			return;
		}
		if (!strcmp(name, "fmp-phase-abort")) {
			payload_mm_authvar_fmp_state_activation_abort();
			payload_mm_authvar_fmp_state_activation_abort();
			assert(payload_mm_authvar_fmp_test_phase() == 3U &&
				!payload_mm_authvar_fmp_test_installed() &&
				fail_closed_count == 1U);
			return;
		}
		if (!strcmp(name, "fmp-phase-abort-policy-mutate")) {
			payload_mm_authvar_fmp_state_activation_abort();
			assert(fail_closed_count == 1U &&
				payload_mm_authvar_fmp_test_phase() == 3U);
			payload_mm_authvar_fmp_test_corrupt_offset(0U, false);
			payload_mm_authvar_fmp_state_activation_abort();
			assert(fail_closed_count == 2U &&
				!payload_mm_authvar_fmp_test_installed() &&
				payload_mm_authvar_fmp_test_phase() == 3U);
			return;
		}
		if (!strcmp(name, "fmp-phase-abort-busy")) {
			fmp_abort_on_begin = true;
			assert(payload_mm_authvar_fmp_state_activation_read(&checked) ==
				PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && !fmp_abort_on_begin &&
				payload_mm_authvar_fmp_test_phase() == 3U &&
				!payload_mm_authvar_fmp_test_installed() &&
				fail_closed_count == 1U);
			payload_mm_authvar_fmp_state_activation_abort();
			payload_mm_authvar_fmp_state_activation_abort();
			assert(fail_closed_count == 1U &&
				payload_mm_authvar_fmp_test_phase() == 3U);
			return;
		}
		checked = output;
		checked.sequence = 1U;
		fmp_activation_read_count = 0U;
		fmp_install_checked_owner(&checked);
		assert(fmp_activation_read_count == 1U);
		assert(payload_mm_fmp_owner_ready() &&
			payload_mm_authvar_fmp_test_installed() &&
			payload_mm_authvar_fmp_test_phase() == 1U);
		assert(payload_mm_authvar_fmp_state_activate() == CB_SUCCESS &&
			payload_mm_authvar_fmp_test_phase() == 2U &&
			payload_mm_authvar_fmp_state_activate() == CB_ERR &&
			!payload_mm_authvar_fmp_state_reconciliation_retryable());
		rejected = sentinel;
		assert(payload_mm_authvar_fmp_state_activation_read(&rejected) ==
			PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)));
		assert(payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &checked) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && checked.present && !poisoned);
		return;
	}
	if (!strcmp(name, "fmp-initialize-source-modes-invalid")) {
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		const struct payload_mm_authvar_store_entry *vendor;
		struct payload_mm_fmp_owner_record rejected = sentinel;

		(void)fmp_store_entry(&index, entries, ARRAY_SIZE(entries));
		vendor = payload_mm_authvar_mode_find(&index,
			PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV);
		assert(vendor);
		media[FV_HEADER_SIZE + vendor->record_offset + 2U] =
			PAYLOAD_MM_AUTHVAR_STATE_DELETED;
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			fail_closed_count == 1U && begin_count == 1U && end_count == 1U);
		assert(!payload_mm_authvar_fmp_state_reconciliation_retryable());
		payload_mm_authvar_fmp_state_activation_abort();
		payload_mm_authvar_fmp_state_activation_abort();
		assert(fail_closed_count == 1U);
		status = payload_mm_authvar_fmp_state_initialize(&rejected);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
			fail_closed_count == 1U && begin_count == 1U && end_count == 1U);
		return;
	}
	if (!strcmp(name, "fmp-initialize-policy-live") ||
	    !strcmp(name, "fmp-initialize-policy-sealed")) {
		struct payload_mm_fmp_owner_record rejected = sentinel;

		payload_mm_authvar_fmp_test_corrupt_offset(0U,
			!strcmp(name, "fmp-initialize-policy-sealed"));
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!payload_mm_authvar_fmp_test_installed() &&
			!memcmp(&output, &sentinel, sizeof(output)) && !begin_count &&
			!end_count && fail_closed_count == 1U);
		status = payload_mm_authvar_fmp_state_initialize(&rejected);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
			fail_closed_count == 1U && !begin_count && !end_count);
		return;
	}
	if (!strcmp(name, "fmp-initialize-suffixed-isolation")) {
		struct payload_mm_fmp_state_identity exact;
		struct payload_mm_fmp_state_identity near_name;
		struct payload_mm_fmp_state_identity wrong_guid;
		struct payload_mm_fmp_state_identity unsuffixed;
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		const struct payload_mm_authvar_store_entry *entry;
		uint32_t near_value = 0x11111111U;
		uint32_t guid_value = 0x22222222U;
		uint32_t unsuffixed_value = 0x33333333U;
		uint32_t version = 0x44444444U;
		uint32_t observed;

		assert(payload_mm_fmp_owner_authvar_identity(
			PAYLOAD_MM_FMP_STATE_KEY_VERSION, &exact) == CB_SUCCESS);
		assert(exact.hardware_instance == payload_mm_fmp_test_hardware_instance);
		assert(exact.variable_name_bytes ==
			(sizeof("FmpVersion") - 1U + 16U + 1U) * sizeof(uint16_t));
		near_name = exact;
		near_name.variable_name[0] ^= 1U;
		wrong_guid = exact;
		wrong_guid.namespace_guid.b[1] ^= 1U;
		unsuffixed = exact;
		unsuffixed.hardware_instance = 0U;
		memset(unsuffixed.variable_name + sizeof("FmpVersion") - 1U, 0,
			sizeof(unsuffixed.variable_name) -
				(sizeof("FmpVersion") - 1U) * sizeof(uint16_t));
		unsuffixed.variable_name_bytes = sizeof("FmpVersion") * sizeof(uint16_t);
		fmp_append_identity_value(&near_name, &near_value, sizeof(near_value),
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		fmp_append_identity_value(&wrong_guid, &guid_value, sizeof(guid_value),
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		fmp_append_identity_value(&unsuffixed, &unsuffixed_value,
			sizeof(unsuffixed_value),
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, version);
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && !poisoned);
		fmp_assert_version_record(&output, version);
		(void)fmp_store_entry(&index, entries, ARRAY_SIZE(entries));
		entry = payload_mm_authvar_store_find(&index,
			near_name.namespace_guid.b, near_name.variable_name,
			near_name.variable_name_bytes);
		assert(entry && entry->attributes ==
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
			entry->data_size == sizeof(observed));
		memcpy(&observed, payload_mm_authvar_store_data(&index, entry),
			sizeof(observed));
		assert(observed == near_value);
		entry = payload_mm_authvar_store_find(&index,
			wrong_guid.namespace_guid.b, wrong_guid.variable_name,
			wrong_guid.variable_name_bytes);
		assert(entry && entry->attributes ==
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
			entry->data_size == sizeof(observed));
		memcpy(&observed, payload_mm_authvar_store_data(&index, entry),
			sizeof(observed));
		assert(observed == guid_value);
		entry = payload_mm_authvar_store_find(&index,
			unsuffixed.namespace_guid.b, unsuffixed.variable_name,
			unsuffixed.variable_name_bytes);
		assert(entry && entry->attributes ==
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
			entry->data_size == sizeof(observed));
		memcpy(&observed, payload_mm_authvar_store_data(&index, entry),
			sizeof(observed));
		assert(observed == unsuffixed_value);
		entry = payload_mm_authvar_store_find(&index, exact.namespace_guid.b,
			exact.variable_name, exact.variable_name_bytes);
		assert(entry == NULL);
		return;
	}
	if (!strcmp(name, "fmp-initialize-ready-to-boot") ||
	    !strcmp(name, "fmp-initialize-runtime")) {
		struct payload_mm_authvar_policy_request lifecycle = {
			.operation = !strcmp(name, "fmp-initialize-runtime") ?
				PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME :
				PAYLOAD_MM_AUTHVAR_SERVICE_READY_TO_BOOT,
		};
		struct payload_mm_authvar_policy_result result;
		unsigned int begins;
		unsigned int ends;

		assert(payload_mm_authvar_policy_transaction(&lifecycle, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		begins = begin_count;
		ends = end_count;
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			begin_count == begins && end_count == ends && !poisoned);
		return;
	}
	if (!strncmp(name, "fmp-initialize-wrong-size-combined", 34U)) {
		uint8_t malformed_state[PAYLOAD_MM_FMP_STATE_WIRE_SIZE + 1U] = {
			0xa5, 0x5a, 0x33, 0xcc,
		};
		uint32_t version;
		size_t malformed_size = sizeof(uint32_t);
		bool force_zero_size = false;

		if (!strcmp(name, "fmp-initialize-wrong-size-combined-zero")) {
			malformed_size = 1U;
			force_zero_size = true;
		} else if (!strcmp(name,
			"fmp-initialize-wrong-size-combined-oversize"))
			malformed_size = sizeof(malformed_state);
		else
			assert(!strcmp(name, "fmp-initialize-wrong-size-combined"));

		fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_STATE, malformed_state,
			malformed_size, PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		if (force_zero_size) {
			struct payload_mm_authvar_store_entry entries[64];
			struct payload_mm_authvar_store_index index;
			const struct payload_mm_authvar_store_entry *entry =
				fmp_store_entry(&index, entries, ARRAY_SIZE(entries));

			assert(entry);
			put32(media + FV_HEADER_SIZE + entry->record_offset + 40U, 0U);
			media[FV_HEADER_SIZE + entry->data_offset] = 0xffU;
			status = payload_mm_authvar_fmp_state_initialize(&output);
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
				!memcmp(&output, &sentinel, sizeof(output)));
			return;
		}
		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0x12345678U);
		status = payload_mm_authvar_fmp_state_initialize(&output);
		memcpy(&version, output.data + 4U, sizeof(version));
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			output.data[0] == 1U && version == 0x12345678U && !poisoned);
		return;
	}
	if (!strncmp(name, "fmp-initialize-wrong-size-legacy", 32U)) {
		uint8_t malformed_version[sizeof(uint64_t)] = {
			0xaa, 0x55,
		};
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		size_t malformed_size = sizeof(uint16_t);
		bool force_zero_size = false;

		if (!strcmp(name, "fmp-initialize-wrong-size-legacy-zero")) {
			malformed_size = 1U;
			force_zero_size = true;
		} else if (!strcmp(name,
			"fmp-initialize-wrong-size-legacy-oversize"))
			malformed_size = sizeof(malformed_version);
		else
			assert(!strcmp(name, "fmp-initialize-wrong-size-legacy"));

		fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_VERSION, malformed_version,
			malformed_size, PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		if (force_zero_size) {
			const struct payload_mm_authvar_store_entry *entry =
				fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index,
					entries, ARRAY_SIZE(entries));

			assert(entry);
			put32(media + FV_HEADER_SIZE + entry->record_offset + 40U, 0U);
			media[FV_HEADER_SIZE + entry->data_offset] = 0xffU;
			status = payload_mm_authvar_fmp_state_initialize(&output);
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
				!memcmp(&output, &sentinel, sizeof(output)));
			return;
		}
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			output.data[0] == 0U && !poisoned &&
			fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index, entries,
				ARRAY_SIZE(entries)) != NULL);
		return;
	}
	if (!strcmp(name, "fmp-initialize-legacy-wrong-attributes")) {
		uint32_t version = 7U;
		unsigned int programs;

		fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &version,
			sizeof(version), PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
		programs = program_count;
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			program_count == programs);
		return;
	}
	if (!strcmp(name, "fmp-initialize-combined-malformed-legacy")) {
		uint8_t wire[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = { 1U, 0U, 0U, 0U };
		uint32_t version = 9U;
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;

		fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_STATE, wire, sizeof(wire),
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &version,
			sizeof(version), PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!memcmp(output.data, wire, sizeof(wire)) && !poisoned &&
			fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index, entries,
				ARRAY_SIZE(entries)) != NULL);
		return;
	}
	if (!strncmp(name, "fmp-initialize-odd-attrs-", 25U) ||
	    !strncmp(name, "fmp-initialize-combined-odd-attrs-", 34U)) {
		bool combined = !strncmp(name, "fmp-initialize-combined-", 24U);
		const char *slot_text = name + (combined ? 34U : 25U);
		uint32_t slot;
		uint32_t value = 0x12345678U;
		uint8_t wire[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = { 1U, 0U, 0U, 0U };
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;

		assert(slot_text[0] >= '0' && slot_text[0] <= '3' && !slot_text[1]);
		slot = (uint32_t)(slot_text[0] - '0');
		if (combined)
			fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_STATE, wire, sizeof(wire),
				PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_VERSION + slot, &value,
			sizeof(value), PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
		status = payload_mm_authvar_fmp_state_initialize(&output);
		if (!combined) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
				!memcmp(&output, &sentinel, sizeof(output)));
		} else {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
				!memcmp(output.data, wire, sizeof(wire)) && !poisoned &&
				fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION + slot,
					&index, entries, ARRAY_SIZE(entries)) != NULL);
		}
		return;
	}
	if (!strcmp(name, "fmp-initialize-duplicate") ||
	    !strcmp(name, "fmp-initialize-legal-transition")) {
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		const struct payload_mm_authvar_store_entry *first;
		uint32_t value;

		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 1U);
		first = fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index, entries,
			ARRAY_SIZE(entries));
		assert(first);
		if (!strcmp(name, "fmp-initialize-legal-transition"))
			media[FV_HEADER_SIZE + first->record_offset + 2U] =
				PAYLOAD_MM_AUTHVAR_STATE_ADDED_IN_DELETED_TRANSITION;
		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 2U);
		status = payload_mm_authvar_fmp_state_initialize(&output);
		if (!strcmp(name, "fmp-initialize-duplicate")) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
				!memcmp(&output, &sentinel, sizeof(output)));
		} else {
			memcpy(&value, output.data + 4U, sizeof(value));
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
				output.data[0] == 1U && value == 2U && !poisoned);
		}
		return;
	}
	if (!strncmp(name, "fmp-initialize-lsv-", 19U)) {
		uint32_t lsv;
		uint32_t observed_lsv;

		if (!strcmp(name, "fmp-initialize-lsv-below-trusted"))
			lsv = 1U;
		else if (!strcmp(name, "fmp-initialize-lsv-equal-trusted"))
			lsv = 7U;
		else {
			assert(!strcmp(name, "fmp-initialize-lsv-above-trusted"));
			lsv = 9U;
		}

		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_LOWEST_VERSION, lsv);
		status = payload_mm_authvar_fmp_state_initialize(&output);
		memcpy(&observed_lsv, output.data + 8U, sizeof(observed_lsv));
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			output.data[1] == 1U && observed_lsv == lsv && !poisoned);
		return;
	}
	if (!strcmp(name, "fmp-initialize-final-end-mutate-authoritative")) {
		fmp_mutate_at_end = 2U;
		fmp_mutate_workspace_session = 6U;
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			!fmp_mutate_at_end && !fmp_mutate_workspace_session &&
			begin_count == 6U && end_count == 6U && fail_closed_count == 1U);
		return;
	}
	if (!strcmp(name, "fmp-initialize-final-end-mutate-published")) {
		fmp_callback_published = &output;
		fmp_mutate_caller = 4U;
		fmp_mutate_caller_session = 6U;
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!output.sequence && !fmp_mutate_caller &&
			!fmp_mutate_caller_session && !poisoned);
		for (size_t i = 0; i < sizeof(output.data); i++)
			assert(output.data[i] == 0U);
		return;
	}
	if (!strncmp(name, "fmp-initialize-reentry-", 23U)) {
		assert(name[23] >= '1' && name[23] <= '6' && !name[24]);
		fmp_initialize_reenter_session = (unsigned int)(name[23] - '0');
		fmp_nested_output = sentinel;
		fmp_nested_status = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			fmp_nested_status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&fmp_nested_output, &sentinel,
				sizeof(fmp_nested_output)) &&
			!fmp_initialize_reenter_session && begin_count == 6U &&
			end_count == 6U && !poisoned);
		return;
	}
	if (!strcmp(name, "fmp-read-does-not-migrate") ||
	    !strcmp(name, "fmp-cas-does-not-migrate")) {
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		unsigned int programs;

		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0x12345678U);
		programs = program_count;
		status = payload_mm_authvar_fmp_state_transaction(
			!strcmp(name, "fmp-read-does-not-migrate") ?
				PAYLOAD_MM_AUTHVAR_FMP_READ_STATE :
				PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE,
			!strcmp(name, "fmp-read-does-not-migrate") ? NULL : &current,
			!strcmp(name, "fmp-read-does-not-migrate") ? NULL : &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED && !poisoned &&
			fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index, entries,
				ARRAY_SIZE(entries)) != NULL);
		assert(!memcmp(&output, &sentinel, sizeof(output)) &&
			program_count == programs);
		return;
	}
	if (!strcmp(name, "fmp-initialize-authoritative-end-fail") ||
	    !strcmp(name, "fmp-initialize-final-begin-fail") ||
	    !strcmp(name, "fmp-initialize-final-read-fail") ||
	    !strcmp(name, "fmp-initialize-final-end-fail")) {
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		struct payload_mm_fmp_owner_record retry;

		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0x12345678U);
		fmp_fail_session = !strcmp(name,
			"fmp-initialize-authoritative-end-fail") ? 1U : 6U;
		if (!strcmp(name, "fmp-initialize-final-begin-fail"))
			fmp_fail_callback = TEST_CALLBACK_BEGIN;
		else if (!strcmp(name, "fmp-initialize-final-read-fail"))
			fmp_fail_callback = TEST_CALLBACK_READ;
		else
			fmp_fail_callback = TEST_CALLBACK_END;
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&output, &sentinel, sizeof(output)) && !poisoned);
		assert(payload_mm_authvar_fmp_state_reconciliation_retryable());
		memset(&retry, 0xa5, sizeof(retry));
		assert(payload_mm_authvar_fmp_state_initialize(&retry) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && !poisoned &&
			fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index, entries,
				ARRAY_SIZE(entries)) == NULL);
		fmp_assert_version_record(&retry, 0x12345678U);
		return;
	}
	if (!strncmp(name, "fmp-initialize-cleanup-fail-", 28U)) {
		const char *failure = name + 28U;
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		struct payload_mm_fmp_owner_record retry;

		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0x12345678U);
		fmp_fail_session = 2U;
		if (!strcmp(failure, "begin"))
			fmp_fail_callback = TEST_CALLBACK_BEGIN;
		else if (!strcmp(failure, "read"))
			fmp_fail_callback = TEST_CALLBACK_READ;
		else if (!strcmp(failure, "program"))
			fmp_fail_callback = TEST_CALLBACK_PROGRAM;
		else {
			assert(!strcmp(failure, "end"));
			fmp_fail_callback = TEST_CALLBACK_END;
		}
		status = payload_mm_authvar_fmp_state_initialize(&output);
		if (!strcmp(failure, "end")) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
				!memcmp(&output, &sentinel, sizeof(output)) && !poisoned);
		} else {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
				!poisoned && fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION,
					&index, entries, ARRAY_SIZE(entries)) != NULL);
		}
		memset(&retry, 0xa5, sizeof(retry));
		if (!strcmp(failure, "end")) {
			assert(payload_mm_authvar_fmp_state_initialize(&retry) ==
				PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
				fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index,
					entries, ARRAY_SIZE(entries)) == NULL && !poisoned);
			fmp_assert_version_record(&retry, 0x12345678U);
		} else {
			assert(payload_mm_authvar_fmp_state_initialize(&retry) ==
				PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
				!memcmp(&retry, &sentinel, sizeof(retry)) && !poisoned);
		}
		return;
	}
	if (!strcmp(name, "fmp-initialize-combined-wins")) {
		uint8_t wire[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = {
			0xff, 0x7f, 2U, 1U, 1U, 2U, 3U, 4U,
		};
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		const struct payload_mm_authvar_store_entry *entry;
#ifndef EXECUTOR_REAL_MEDIA
		uint32_t record_offset;
		uint32_t record_end;
#endif
		struct payload_mm_fmp_state_identity version_identity;

		fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_STATE, wire, sizeof(wire),
			PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0xdeadbeefU);
		entry = fmp_store_entry(&index, entries, ARRAY_SIZE(entries));
		assert(entry);
#ifndef EXECUTOR_REAL_MEDIA
		record_offset = entry->record_offset;
		record_end = ALIGN_UP(entry->data_offset + entry->data_size, 4U);
#endif
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!memcmp(output.data, wire, sizeof(wire)) && !poisoned);
		entry = fmp_store_entry(&index, entries, ARRAY_SIZE(entries));
		assert(entry &&
#ifndef EXECUTOR_REAL_MEDIA
			entry->record_offset == record_offset &&
#endif
			!memcmp(payload_mm_authvar_store_data(&index, entry), wire,
				sizeof(wire)));
		assert(payload_mm_fmp_owner_authvar_identity(
			PAYLOAD_MM_FMP_STATE_KEY_VERSION, &version_identity) == CB_SUCCESS &&
			payload_mm_authvar_store_find(&index,
				version_identity.namespace_guid.b,
				version_identity.variable_name,
				version_identity.variable_name_bytes) == NULL);
#ifndef EXECUTOR_REAL_MEDIA
		for (size_t i = 0; i < program_count; i++)
			assert(write_trace_disjoint(&program_trace[i],
				FV_HEADER_SIZE + record_offset, record_end - record_offset));
		for (size_t i = 0; i < erase_count; i++)
			assert(write_trace_disjoint(&erase_trace[i],
				FV_HEADER_SIZE + record_offset, record_end - record_offset));
#endif
		return;
	}
	if (!strncmp(name, "fmp-initialize-classifications-", 31U)) {
		unsigned int vector = 0U;
		unsigned int remaining;
		unsigned int classes[4];
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		struct payload_mm_fmp_owner_record second;
		unsigned int programs;

		for (const char *digit = name + 31U; *digit; digit++) {
			assert(*digit >= '0' && *digit <= '9' && vector <= 80U);
			vector = vector * 10U + (unsigned int)(*digit - '0');
		}
		assert(vector <= 80U);
		remaining = vector;
		for (uint32_t slot = 0; slot < ARRAY_SIZE(classes); slot++) {
			uint32_t value = 0x10203040U + slot;
			uint16_t short_value = (uint16_t)value;

			classes[slot] = remaining % 3U;
			remaining /= 3U;
			if (classes[slot] == 1U)
				fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION + slot,
					value);
			else if (classes[slot] == 2U)
				fmp_append_value(PAYLOAD_MM_FMP_STATE_KEY_VERSION + slot,
					&short_value, sizeof(short_value),
					PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES);
		}
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!poisoned);
		for (uint32_t slot = 0; slot < ARRAY_SIZE(classes); slot++) {
			uint32_t value;
			const struct payload_mm_authvar_store_entry *entry;

			memcpy(&value, output.data + 4U + slot * 4U, sizeof(value));
			assert(output.data[slot] == (classes[slot] == 1U) &&
				value == (classes[slot] == 1U ? 0x10203040U + slot : 0U));
			entry = fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION + slot,
				&index, entries, ARRAY_SIZE(entries));
			assert((entry != NULL) == (classes[slot] == 2U));
		}
		programs = program_count;
		memset(&second, 0xa5, sizeof(second));
		assert(payload_mm_authvar_fmp_state_initialize(&second) ==
			PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
			!memcmp(&second, &sentinel, sizeof(second)) &&
			program_count == programs && !poisoned);
		return;
	}
	if (!strncmp(name, "fmp-initialize-mask-", 20U)) {
		unsigned int mask = 0U;
		struct payload_mm_fmp_owner_record second;
		struct payload_mm_authvar_store_entry entries[64];
		struct payload_mm_authvar_store_index index;
		struct payload_mm_fmp_state_identity identity;
		unsigned int programs;

		for (const char *digit = name + 20U; *digit; digit++) {
			assert(*digit >= '0' && *digit <= '9' && mask <= 15U);
			mask = mask * 10U + (unsigned int)(*digit - '0');
		}
		assert(mask <= 15U);

		for (uint32_t slot = 0; slot < 4U; slot++)
			if (mask & (1U << slot))
				fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION + slot,
					slot ? 0x10203040U + slot : 0U);
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!output.sequence && !poisoned);
		for (uint32_t slot = 0; slot < 4U; slot++) {
			uint32_t value;

			memcpy(&value, output.data + 4U + slot * 4U, sizeof(value));
			assert(output.data[slot] == !!(mask & (1U << slot)) &&
				value == ((mask & (1U << slot)) ?
					(slot ? 0x10203040U + slot : 0U) : 0U));
		}
		assert(fmp_store_entry(&index, entries, ARRAY_SIZE(entries)) != NULL);
		for (uint32_t key = PAYLOAD_MM_FMP_STATE_KEY_VERSION;
		     key <= PAYLOAD_MM_FMP_STATE_KEY_LAST_ATTEMPT_VERSION; key++) {
			assert(payload_mm_fmp_owner_authvar_identity(key, &identity) ==
				CB_SUCCESS);
			assert(payload_mm_authvar_store_find(&index,
				identity.namespace_guid.b, identity.variable_name,
				identity.variable_name_bytes) == NULL);
		}
		programs = program_count;
		memset(&second, 0xa5, sizeof(second));
		assert(payload_mm_authvar_fmp_state_initialize(&second) ==
			PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
			!memcmp(&second, &sentinel, sizeof(second)) && program_count == programs &&
			begin_count == end_count && !poisoned);
		return;
	}
	if (!strcmp(name, "fmp-initialize-empty")) {
		status = payload_mm_authvar_fmp_state_initialize(&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			output.attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
			output.data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE &&
			!output.sequence && begin_count == 6U && end_count == 6U &&
			!poisoned);
		for (size_t i = 0; i < sizeof(output.data); i++)
			assert(output.data[i] == 0U);
		return;
	}
	if (!strcmp(name, "fmp-layout")) {
		struct payload_mm_authvar_fmp_layout_test layout;
		const size_t sizes[] = {
			sizeof(struct payload_mm_fmp_state_identity),
			sizeof(struct payload_mm_fmp_owner_record),
			sizeof(struct payload_mm_fmp_owner_record),
			sizeof(struct payload_mm_fmp_owner_record),
		};

		assert(payload_mm_authvar_fmp_test_layout(&layout));
		for (size_t i = 0; i < ARRAY_SIZE(sizes); i++) {
			assert(layout.offsets[i] &&
				!(layout.offsets[i] % __BIGGEST_ALIGNMENT__));
			assert(layout.offsets[i] <= layout.required_size - sizes[i]);
			if (i)
				assert(layout.offsets[i] >=
					layout.offsets[i - 1U] + sizes[i - 1U]);
			for (size_t j = 0; j < layout.protected_count; j++) {
				assert(layout.protected_sizes[j] &&
					layout.protected_offsets[j] <= layout.required_size -
						layout.protected_sizes[j]);
				assert(layout.offsets[i] + sizes[i] <=
						layout.protected_offsets[j] ||
					layout.protected_offsets[j] +
						layout.protected_sizes[j] <= layout.offsets[i]);
			}
		}
		return;
	}
	payload_mm_authvar_fmp_test_force_active();
	if (!strcmp(name, "fmp-offset-corrupt") ||
	    !strncmp(name, "fmp-policy-", 11U)) {
		static const char *const field_names[] = {
			"identity", "current", "candidate", "observation",
		};
		unsigned int part = 3U;
		bool sealed = false;
		struct payload_mm_fmp_owner_record rejected = sentinel;

		if (strcmp(name, "fmp-offset-corrupt")) {
			const char *field = name + 11U;

			if (!strncmp(field, "sealed-", 7U)) {
				sealed = true;
				field += 7U;
			} else {
				assert(!strncmp(field, "live-", 5U));
				field += 5U;
			}
			for (part = 0; part < ARRAY_SIZE(field_names); part++)
				if (!strcmp(field, field_names[part]))
					break;
			assert(part < ARRAY_SIZE(field_names));
		}
		payload_mm_authvar_fmp_test_corrupt_offset(part, sealed);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!payload_mm_authvar_fmp_test_installed() &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			!begin_count && !end_count && !program_count && !erase_count &&
			fail_closed_count == 1U);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &rejected);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
			!begin_count && !end_count && !program_count && !erase_count &&
			fail_closed_count == 1U);
		return;
	}
	if (!strcmp(name, "fmp-read")) {
		struct payload_mm_authvar_ftw_plan plan;

		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			!output.present && !output.sequence && !poisoned &&
			begin_count == 1U && end_count == 1U && !program_count &&
			!erase_count);
		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		return;
	}
	if (!strcmp(name, "fmp-transient-read")) {
		struct payload_mm_authvar_ftw_plan plan;

		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!candidate_media_fault_armed && !poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			begin_count == 1U && end_count == 1U);
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			!output.present && !output.sequence && !poisoned &&
			begin_count == 2U && end_count == 2U && !program_count &&
			!erase_count);
		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		return;
	}
	if (!strcmp(name, "fmp-end-failure"))
		fail_end = true;
	if (!strcmp(name, "fmp-tandem-end"))
		fmp_tandem_end = true;
	if (!strcmp(name, "fmp-tandem-tail-end")) {
		fmp_tandem_end = true;
		fmp_tandem_tail = true;
	}
	if (!strcmp(name, "fmp-tandem-program"))
		fmp_tandem_phase = 1;
	if (!strcmp(name, "fmp-tandem-tail-program")) {
		fmp_tandem_phase = 1;
		fmp_tandem_tail = true;
	}
	if (!strcmp(name, "fmp-tandem-read"))
		fmp_tandem_phase = 2;
	if (!strncmp(name, "fmp-end-mutate-", 15U))
		fmp_mutate_at_end = (unsigned int)(name[15] - '0') + 1U;
	if (!strcmp(name, "fmp-control-phase-corrupt"))
		fmp_control_mutate_at_end = 1U;
	if (!strcmp(name, "fmp-control-digest-corrupt"))
		fmp_control_mutate_at_end = 2U;
	if (!strcmp(name, "fmp-invalid-transition")) {
		candidate.data[0] = 2;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
			!memcmp(&output, &sentinel, sizeof(output)) && !begin_count);
		return;
	}
	if (!strcmp(name, "fmp-malformed-absent-padding")) {
		current.data[0] = 1U;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
			!memcmp(&output, &sentinel, sizeof(output)) && !begin_count &&
			!end_count && !program_count && !erase_count && !poisoned);
		return;
	}
	if (!strcmp(name, "fmp-trusted-lsv-floor")) {
		struct payload_mm_authvar_ftw_plan plan;

		candidate.data[1] = 1U;
		put32(candidate.data + 8U, 6U);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
			!memcmp(&output, &sentinel, sizeof(output)) && !begin_count &&
			!end_count && !program_count && !erase_count && !poisoned);
		put32(candidate.data + 8U, 7U);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!output.sequence && !poisoned && begin_count == 1U &&
			end_count == 1U);
		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		return;
	}
	if (!strcmp(name, "fmp-monotonic-lsv")) {
		candidate.data[1] = 1U;
		put32(candidate.data + 8U, 9U);
	}
	if (!strcmp(name, "fmp-transient-program")) {
		candidate_media_fault = CANDIDATE_MEDIA_FAULT_PROGRAM;
		candidate_media_fault_result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		candidate_media_fault_armed = true;
	}
	if (!strcmp(name, "fmp-sha-failure")) {
		memcpy(fmp_sha_media_before, media, sizeof(fmp_sha_media_before));
		fmp_sha_fail = true;
	}
	if (!strcmp(name, "fmp-active-first-persist"))
		fmp_require_first_persistence_binding = true;
	status = payload_mm_authvar_fmp_state_transaction(
		PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
		&output);
	if (!strcmp(name, "fmp-end-failure")) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&output, &sentinel, sizeof(output)));
		return;
	}
	if (!strncmp(name, "fmp-end-mutate-", 15U) ||
	    !strncmp(name, "fmp-control-", 12U)) {
		struct payload_mm_fmp_owner_record rejected = sentinel;
		unsigned int programs = program_count;
		unsigned int erases = erase_count;

		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!payload_mm_authvar_fmp_test_installed() &&
			!memcmp(&output, &sentinel, sizeof(output)) && begin_count == 1U &&
			end_count == 1U && fail_closed_count == 1U);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &rejected);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
			begin_count == 1U && end_count == 1U &&
			fail_closed_count == 1U && program_count == programs &&
			erase_count == erases);
		return;
	}
	if (!strcmp(name, "fmp-tandem-end")) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)));
		return;
	}
	if (!strncmp(name, "fmp-tandem-", 11U)) {
		if (!(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
		      !memcmp(&output, &sentinel, sizeof(output))))
			dprintf(2, "%s status=%llx poison=%u phase=%u off=%u\n", name,
				(unsigned long long)status, poisoned, fmp_tandem_phase,
				fmp_record_media_offset);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)));
		return;
	}
	if (!strcmp(name, "fmp-sha-failure")) {
		struct payload_mm_fmp_owner_record rejected = sentinel;
		unsigned int programs = program_count;
		unsigned int erases = erase_count;

		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!fmp_sha_fail && !memcmp(&output, &sentinel, sizeof(output)) &&
			begin_count == 1U && end_count == 1U && fail_closed_count == 1U &&
			fmp_record_sha_calls >= 1U && !program_count && !erase_count &&
			!memcmp(media, fmp_sha_media_before, sizeof(fmp_sha_media_before)));
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &rejected);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
			begin_count == 1U && end_count == 1U && fail_closed_count == 1U &&
			program_count == programs && erase_count == erases &&
			!memcmp(media, fmp_sha_media_before, sizeof(fmp_sha_media_before)));
		return;
	}
	if (!strcmp(name, "fmp-transient-program")) {
		struct payload_mm_authvar_ftw_plan plan;

		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!candidate_media_fault_armed && !poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			begin_count == 1U && end_count == 1U);
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			!output.present && !output.sequence && !poisoned &&
			begin_count == 2U && end_count == 2U);
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!output.sequence && !poisoned && begin_count == 3U &&
			end_count == 3U);
		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		return;
	}
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
		output.attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
		output.data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE && !output.sequence);
	if (!strcmp(name, "fmp-active-first-persist")) {
		assert(!fmp_require_first_persistence_binding && program_count &&
			!poisoned && !fail_closed_count);
		return;
	}
	if (!strcmp(name, "fmp-success"))
		return;
	if (!strcmp(name, "fmp-monotonic-lsv")) {
		struct payload_mm_authvar_ftw_plan plan;

		current = candidate;
		candidate.sequence++;
		put32(candidate.data + 8U, 8U);
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
			!memcmp(&output, &sentinel, sizeof(output)) && !poisoned &&
			begin_count == 1U && end_count == 1U);
		put32(candidate.data + 8U, 10U);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			!output.sequence && !poisoned && begin_count == 2U &&
			end_count == 2U);
		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		return;
	}
	if (!strcmp(name, "fmp-recovery-abort-old") ||
	    !strcmp(name, "fmp-recovery-replay-new") ||
	    !strcmp(name, "fmp-recovery-malformed")) {
		static uint8_t old_primary[BLOCK_SIZE];
		struct payload_mm_authvar_ftw_plan plan;
		bool abort_old = !strcmp(name, "fmp-recovery-abort-old");
		bool malformed = !strcmp(name, "fmp-recovery-malformed");
		unsigned int programs_before;
		unsigned int erases_before;

		memcpy(old_primary, media, sizeof(old_primary));
		current = candidate;
		candidate.sequence++;
		candidate.data[2] = 1U;
		candidate.data[3] = 1U;
		candidate.data[12] = 1U;
		candidate.data[16] = 1U;
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			output.data[2] == 1U && !output.sequence);
		memcpy(spare(), media, BLOCK_SIZE);
		memcpy(media, old_primary, sizeof(old_primary));
		make_write(abort_old ? PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED :
			PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		if (malformed)
			spare()[BLOCK_SIZE] = 0x7fU;
		programs_before = program_count;
		erases_before = erase_count;
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		if (malformed) {
			struct payload_mm_fmp_owner_record rejected = sentinel;
			enum cb_err plan_status;

			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
				poisoned && fail_closed_count == 1U &&
				!memcmp(&output, &sentinel, sizeof(output)) &&
				begin_count == 3U && end_count == 3U &&
				program_count == programs_before && erase_count == erases_before);
			plan_status = payload_mm_authvar_ftw_plan(media, MEDIA_SIZE,
				BLOCK_SIZE, &plan);
			assert(plan_status == CB_SUCCESS &&
				plan.action == PAYLOAD_MM_AUTHVAR_FTW_REPLAY_SPARE);
			status = payload_mm_authvar_fmp_state_transaction(
				PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &rejected);
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
				!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
				begin_count == 3U && end_count == 3U &&
				fail_closed_count == 1U &&
				program_count == programs_before && erase_count == erases_before);
			return;
		}
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			output.data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE &&
			output.data[2] == (abort_old ? 0U : 1U) && !output.sequence &&
			!poisoned && begin_count == 3U && end_count == 3U &&
			!memcmp(output.data, abort_old ? current.data : candidate.data,
				PAYLOAD_MM_FMP_STATE_WIRE_SIZE) &&
			memcmp(&output, &sentinel, sizeof(output)));
		if (!abort_old)
			assert(program_count > programs_before && erase_count > erases_before);
		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		assert(erased(spare(), 2U * BLOCK_SIZE));
		return;
	}
	if (!strcmp(name, "fmp-replace") || !strcmp(name, "fmp-reclaim") ||
	    !strcmp(name, "fmp-reclaim-tandem-padding-program") ||
	    !strcmp(name, "fmp-reclaim-tandem-end") ||
	    !strcmp(name, "fmp-transient-erase")) {
		current = candidate;
		candidate.sequence++;
		candidate.data[2] = 1;
		candidate.data[3] = 1;
		candidate.data[12] = 1;
		candidate.data[16] = 1;
		if (strcmp(name, "fmp-replace"))
			fmp_dirty_tail();
		if (!strcmp(name, "fmp-reclaim-tandem-padding-program")) {
			fmp_tandem_phase = 1U;
			fmp_tandem_padding = true;
		} else if (!strcmp(name, "fmp-reclaim-tandem-end")) {
			fmp_tandem_end = true;
			fmp_tandem_tail = true;
		}
		if (!strcmp(name, "fmp-transient-erase")) {
			candidate_media_fault = CANDIDATE_MEDIA_FAULT_ERASE;
			candidate_media_fault_result =
				PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
			candidate_media_fault_armed = true;
		}
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
			&output);
		if (!strncmp(name, "fmp-reclaim-tandem-", 19U)) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
				!memcmp(&output, &sentinel, sizeof(output)));
			if (!strcmp(name, "fmp-reclaim-tandem-padding-program"))
				assert(fmp_tandem_attack_program_count &&
					program_count == fmp_tandem_attack_program_count);
			return;
		}
		if (!strcmp(name, "fmp-transient-erase")) {
			struct payload_mm_authvar_ftw_plan plan;

			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
				!candidate_media_fault_armed && !poisoned &&
				!memcmp(&output, &sentinel, sizeof(output)) &&
				erase_count && begin_count == 2U && end_count == 2U);
			output = sentinel;
			status = payload_mm_authvar_fmp_state_transaction(
				PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
				output.data[2] == 0U && !output.sequence && !poisoned &&
				begin_count == 3U && end_count == 3U);
			output = sentinel;
			status = payload_mm_authvar_fmp_state_transaction(
				PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current,
				&candidate, &output);
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
				output.data[2] == 1U && !output.sequence && !poisoned &&
				begin_count == 4U && end_count == 4U);
			assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
				&plan) == CB_SUCCESS &&
				plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
			assert(erased(spare(), 2U * BLOCK_SIZE));
			return;
		}
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
			output.data[2] == 1 && output.data[3] == 1 && !output.sequence &&
			(!strcmp(name, "fmp-replace") || erase_count));
		return;
	}
	if (!strncmp(name, "fmp-malformed-", 14U)) {
		struct payload_mm_authvar_ftw_plan plan;
		struct payload_mm_fmp_owner_record rejected = sentinel;
		unsigned int programs_before = program_count;
		unsigned int erases_before = erase_count;

		if (!strcmp(name, "fmp-malformed-attributes"))
			fmp_mutate_stored(FMP_STORED_ATTRIBUTES);
		else if (!strcmp(name, "fmp-malformed-size"))
			fmp_mutate_stored(FMP_STORED_SIZE);
		else {
			assert(!strcmp(name, "fmp-malformed-data"));
			fmp_mutate_stored(FMP_STORED_DATA);
		}
		output = sentinel;
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &output);
		if (!strcmp(name, "fmp-malformed-data")) {
			assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
				output.data[0] == 2U && !output.sequence && !poisoned &&
				begin_count == 2U && end_count == 2U &&
				program_count == programs_before && erase_count == erases_before);
			return;
		}
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR && poisoned &&
			!memcmp(&output, &sentinel, sizeof(output)) &&
			begin_count == 2U && end_count == 2U && fail_closed_count == 1U &&
			program_count == programs_before && erase_count == erases_before);
		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
		status = payload_mm_authvar_fmp_state_transaction(
			PAYLOAD_MM_AUTHVAR_FMP_READ_STATE, NULL, NULL, &rejected);
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
			!memcmp(&rejected, &sentinel, sizeof(rejected)) &&
			begin_count == 2U && end_count == 2U && fail_closed_count == 1U &&
			program_count == programs_before && erase_count == erases_before);
		return;
	}
	output = sentinel;
	status = payload_mm_authvar_fmp_state_transaction(
		PAYLOAD_MM_AUTHVAR_FMP_COMPARE_WRITE_STATE, &current, &candidate,
		&output);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_ACCESS_DENIED &&
		!memcmp(&output, &sentinel, sizeof(output)));
}
#endif

#ifdef EXECUTOR_REAL_MEDIA
static void reset_initialize(unsigned int cut)
{
	struct payload_mm_authvar_ftw_plan plan;
	int status;
	pid_t child;

	memset(working(), 0xff, BLOCK_SIZE);
	child = fork();
	assert(child >= 0);
	if (!child) {
		reset_operation = cut;
		install();
		(void)payload_mm_authvar_executor_recover();
		_exit(0);
	}
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 77);
	operation_count = 0;
	install();
	assert(payload_mm_authvar_executor_recover() ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE, &plan) ==
		CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
}

static unsigned int parse_cut(const char *text)
{
	unsigned int value = 0;

	assert(*text);
	for (; *text; text++) {
		assert(*text >= '0' && *text <= '9');
		assert(value <= (UINT_MAX - (unsigned int)(*text - '0')) / 10U);
		value = value * 10U + (unsigned int)(*text - '0');
	}
	return value;
}

#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
static void fmp_assert_reset_state(bool require_converged)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	const struct payload_mm_authvar_store_entry *combined;
	const struct payload_mm_authvar_store_entry *version;
	uint32_t value;
	bool old_state;
	bool new_state;

	combined = fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_STATE, &index, entries,
		ARRAY_SIZE(entries));
	version = fmp_key_entry(PAYLOAD_MM_FMP_STATE_KEY_VERSION, &index, entries,
		ARRAY_SIZE(entries));
	old_state = !combined && version &&
		version->attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
		version->data_size == sizeof(value);
	if (old_state) {
		memcpy(&value, payload_mm_authvar_store_data(&index, version),
			sizeof(value));
		old_state = value == 0x12345678U;
	}
	new_state = combined &&
		combined->attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
		combined->data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE;
	if (new_state) {
		const uint8_t *data = payload_mm_authvar_store_data(&index, combined);
		uint8_t expected[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = { 1U };

		memcpy(expected + 4U, &(uint32_t) { 0x12345678U }, sizeof(uint32_t));
		new_state = !memcmp(data, expected, sizeof(expected));
	}
	assert(old_state != new_state);
	if (require_converged)
		assert(new_state && !version);
}

static void fmp_reset_initialize(unsigned int cut, bool force_reclaim)
{
	struct payload_mm_authvar_ftw_plan plan;
	struct payload_mm_fmp_owner_record output;
	int child_status;
	pid_t child;

	fmp_prepare_modes();
	fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0x12345678U);
	if (force_reclaim)
		fmp_dirty_tail();
	child = fork();
	assert(child >= 0);
	if (!child) {
		reset_operation = cut;
		install();
		(void)payload_mm_authvar_fmp_state_initialize(&output);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 77);
	operation_count = 0U;
	install();
	assert(payload_mm_authvar_executor_recover() ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE, &plan) ==
		CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	fmp_assert_reset_state(false);
	memset(&output, 0xa5, sizeof(output));
	assert(payload_mm_authvar_fmp_state_initialize(&output) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS && output.present &&
		output.attributes == PAYLOAD_MM_FMP_STATE_VARIABLE_ATTRIBUTES &&
		output.data_size == PAYLOAD_MM_FMP_STATE_WIRE_SIZE &&
		!output.sequence);
	{
		uint8_t expected[PAYLOAD_MM_FMP_STATE_WIRE_SIZE] = { 1U };

		memcpy(expected + 4U, &(uint32_t) { 0x12345678U }, sizeof(uint32_t));
		assert(!memcmp(output.data, expected, sizeof(expected)));
	}
	fmp_assert_reset_state(true);
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE, &plan) ==
		CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
}
#endif
#endif

static bool erased(const uint8_t *bytes, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (bytes[i] != 0xffU)
			return false;
	return true;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT) || \
	CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
enum payload_mm_verify_status payload_mm_sha256(const void *message,
	size_t message_size, uint8_t digest[PAYLOAD_MM_SHA256_SIZE])
{
	const uint8_t *bytes = message;

#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	{
		size_t buffer_offset;
		size_t record_offset;
		bool separate_image;
		bool fmp_record = payload_mm_authvar_fmp_test_record_target(message,
			message_size, false, false, &buffer_offset, &record_offset,
			&separate_image) && !buffer_offset && !record_offset &&
			!separate_image;

		if (fmp_record)
			fmp_record_sha_calls++;
		if (fmp_sha_fail && fmp_record) {
			fmp_sha_fail = false;
			return PAYLOAD_MM_VERIFY_INTERNAL;
		}
	}
#endif

	memset(digest, 0, PAYLOAD_MM_SHA256_SIZE);
	for (size_t i = 0; i < message_size; i++)
		digest[i % PAYLOAD_MM_SHA256_SIZE] ^=
			(uint8_t)(bytes[i] + (uint8_t)i);
	return PAYLOAD_MM_VERIFY_OK;
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
_Static_assert(sizeof(struct payload_mm_authvar_candidate_result) == 120U,
	"candidate result mutation matrix changed");
enum candidate_prepare_mode {
	CANDIDATE_PREPARE_OK,
	CANDIDATE_PREPARE_BINDING,
	CANDIDATE_PREPARE_HEADER,
	CANDIDATE_PREPARE_SOURCE,
	CANDIDATE_PREPARE_INDEX,
	CANDIDATE_PREPARE_OUTPUT,
	CANDIDATE_PREPARE_GENERATION,
	CANDIDATE_PREPARE_TOKEN,
	CANDIDATE_PREPARE_RUNTIME,
	CANDIDATE_PREPARE_BIND_RESERVED,
	CANDIDATE_PREPARE_POLICY,
	CANDIDATE_PREPARE_SOURCE_USED,
	CANDIDATE_PREPARE_CANDIDATE_USED,
	CANDIDATE_PREPARE_COUNT,
	CANDIDATE_PREPARE_MODES,
	CANDIDATE_PREPARE_MODE_SETUP,
	CANDIDATE_PREPARE_MODE_SECURE,
	CANDIDATE_PREPARE_MODE_VENDOR,
	CANDIDATE_PREPARE_RESULT_RESERVED,
	CANDIDATE_PREPARE_SOURCE_DIGEST,
	CANDIDATE_PREPARE_CANDIDATE_DIGEST,
	CANDIDATE_PREPARE_CANDIDATE_BODY,
	CANDIDATE_PREPARE_PRIMARY_MEDIA,
	CANDIDATE_PREPARE_WORKING_MEDIA,
	CANDIDATE_PREPARE_QUEUE_MEDIA,
	CANDIDATE_PREPARE_SPARE_MEDIA,
	CANDIDATE_PREPARE_IMAGE_CALLBACK,
	CANDIDATE_PREPARE_SOURCE_CALLBACK,
	CANDIDATE_PREPARE_DIRTY_TAIL,
	CANDIDATE_PREPARE_RESULT_BIT,
};

struct candidate_prepare_context {
	enum candidate_prepare_mode mode;
	u8 *published_modes;
	enum candidate_media_fault_kind media_fault;
	enum payload_mm_authvar_media_result media_fault_result;
	size_t result_mutation_offset;
	unsigned int result_mutation_bit;
};

static uint64_t prepare_candidate(
	const struct payload_mm_authvar_store_index *source,
	const struct payload_mm_authvar_candidate_binding *binding,
	void *candidate_buffer, size_t candidate_capacity,
	struct payload_mm_authvar_store_entry *scan_entries,
	size_t scan_entry_capacity,
	struct payload_mm_authvar_candidate_result *result, void *opaque)
{
	static const uint8_t guid[16] = { 0x42U };
	static const uint8_t name[] = { 'A', 0, 0, 0 };
	static const uint8_t data[] = { 0xa5U };
	static const uint8_t timestamp[16] = { 0xe8U, 0x07U, 1U, 1U };
	struct candidate_prepare_context *context = opaque;
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = name,
		.name_size = sizeof(name),
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
	};
	const struct payload_mm_authvar_record_span span = {
		.data = data,
		.size = sizeof(data),
	};
	uint8_t *candidate = candidate_buffer;
	uint32_t record_size;
	size_t record_offset = source->used_size;

	(void)scan_entries;
	(void)scan_entry_capacity;
	assert(candidate_capacity == STORE_SIZE);
	memset(candidate, 0xff, candidate_capacity);
	memcpy(candidate, source->store, source->used_size);
	memcpy(descriptor.vendor_guid, guid, sizeof(guid));
	memcpy(descriptor.timestamp, timestamp, sizeof(timestamp));
	assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
		candidate + record_offset, candidate_capacity - record_offset,
		&record_size));
	candidate[record_offset + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	memset(result, 0, sizeof(*result));
	result->binding = *binding;
	result->policy = (struct payload_mm_authvar_write_policy) {
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_record_size = STORE_SIZE,
		.maximum_records = 64U,
	};
	result->source_used_size = source->used_size;
	result->candidate_used_size = (uint32_t)record_offset + record_size;
	result->candidate_record_count = source->entry_count + 1U;
	result->volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	assert(payload_mm_sha256(source->store, source->store_size,
		result->source_digest) == PAYLOAD_MM_VERIFY_OK);
	assert(payload_mm_sha256(candidate, candidate_capacity,
		result->candidate_digest) == PAYLOAD_MM_VERIFY_OK);
	if (context->mode == CANDIDATE_PREPARE_BINDING)
		result->binding.source_volatile_modes ^= PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	else if (context->mode == CANDIDATE_PREPARE_HEADER) {
		put32(candidate + 16U, STORE_SIZE - 4U);
		assert(payload_mm_sha256(candidate, candidate_capacity,
			result->candidate_digest) == PAYLOAD_MM_VERIFY_OK);
	} else if (context->mode == CANDIDATE_PREPARE_SOURCE)
		((uint8_t *)source->store)[PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE - 1U] ^= 1U;
	else if (context->mode == CANDIDATE_PREPARE_INDEX)
		((struct payload_mm_authvar_store_index *)source)->used_size++;
	else if (context->mode == CANDIDATE_PREPARE_OUTPUT)
		*context->published_modes = 0xffU;
	else if (context->mode == CANDIDATE_PREPARE_GENERATION)
		result->binding.generation++;
	else if (context->mode == CANDIDATE_PREPARE_TOKEN)
		result->binding.token++;
	else if (context->mode == CANDIDATE_PREPARE_RUNTIME)
		result->binding.at_runtime ^= 1U;
	else if (context->mode == CANDIDATE_PREPARE_BIND_RESERVED)
		result->binding.reserved[0] = 1U;
	else if (context->mode == CANDIDATE_PREPARE_POLICY)
		result->policy.maximum_records--;
	else if (context->mode == CANDIDATE_PREPARE_SOURCE_USED)
		result->source_used_size++;
	else if (context->mode == CANDIDATE_PREPARE_CANDIDATE_USED)
		result->candidate_used_size++;
	else if (context->mode == CANDIDATE_PREPARE_COUNT)
		result->candidate_record_count++;
	else if (context->mode == CANDIDATE_PREPARE_MODES)
		result->volatile_modes = 0x80U;
	else if (context->mode == CANDIDATE_PREPARE_MODE_SETUP)
		result->volatile_modes ^= PAYLOAD_MM_AUTHVAR_MODE_SETUP;
	else if (context->mode == CANDIDATE_PREPARE_MODE_SECURE)
		result->volatile_modes ^= PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;
	else if (context->mode == CANDIDATE_PREPARE_MODE_VENDOR)
		result->volatile_modes ^= PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS;
	else if (context->mode == CANDIDATE_PREPARE_RESULT_RESERVED)
		result->reserved[0] = 1U;
	else if (context->mode == CANDIDATE_PREPARE_SOURCE_DIGEST)
		result->source_digest[0] ^= 1U;
	else if (context->mode == CANDIDATE_PREPARE_CANDIDATE_DIGEST)
		result->candidate_digest[0] ^= 1U;
	else if (context->mode == CANDIDATE_PREPARE_CANDIDATE_BODY) {
		candidate[source->used_size] ^= 1U;
		assert(payload_mm_sha256(candidate, candidate_capacity,
			result->candidate_digest) == PAYLOAD_MM_VERIFY_OK);
	} else if (context->mode == CANDIDATE_PREPARE_PRIMARY_MEDIA)
		media[0] ^= 1U;
	else if (context->mode == CANDIDATE_PREPARE_WORKING_MEDIA)
		working()[0] ^= 1U;
	else if (context->mode == CANDIDATE_PREPARE_QUEUE_MEDIA)
		working()[PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE] = 0x7fU;
	else if (context->mode == CANDIDATE_PREPARE_SPARE_MEDIA)
		spare()[0] = 0x7fU;
	else if (context->mode == CANDIDATE_PREPARE_IMAGE_CALLBACK)
		mutate_candidate_header_on_program = true;
	else if (context->mode == CANDIDATE_PREPARE_SOURCE_CALLBACK)
		mutate_candidate_source_on_program = true;
	else if (context->mode == CANDIDATE_PREPARE_DIRTY_TAIL) {
		candidate[record_offset + 2U] =
			PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY;
		assert(payload_mm_sha256(candidate, candidate_capacity,
			result->candidate_digest) == PAYLOAD_MM_VERIFY_OK);
	} else if (context->mode == CANDIDATE_PREPARE_RESULT_BIT) {
		assert(context->result_mutation_offset < sizeof(*result) &&
			context->result_mutation_bit < 8U);
		((uint8_t *)result)[context->result_mutation_offset] ^=
			(uint8_t)(1U << context->result_mutation_bit);
	}
	if (context->media_fault != CANDIDATE_MEDIA_FAULT_NONE) {
		candidate_media_fault = context->media_fault;
		candidate_media_fault_result = context->media_fault_result;
		candidate_media_fault_armed = true;
	}
	return PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
}

static uint8_t candidate_source_vendor_value;

static void make_candidate_source(void)
{
	static const uint8_t vknv_guid[16] = {
		0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
		0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
	};
	static const uint8_t vknv_name[] = {
		'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
		'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
	};
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = vknv_name,
		.name_size = sizeof(vknv_name),
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
	};
	const struct payload_mm_authvar_record_span span = {
		.data = &candidate_source_vendor_value,
		.size = sizeof(candidate_source_vendor_value),
	};
	uint32_t record_size;

	memcpy(descriptor.vendor_guid, vknv_guid, sizeof(vknv_guid));
	assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO,
		media + FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		STORE_SIZE - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, &record_size));
	media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED;
}

static void candidate_commit_case(enum candidate_prepare_mode mode,
	unsigned int fail_at, bool success)
{
	struct candidate_prepare_context context = { .mode = mode };
	u8 published_modes = 0;
	uint64_t status;

	context.published_modes = &published_modes;
	make_candidate_source();
	fail_operation = fail_at;
	install();
	status = payload_mm_authvar_executor_test_commit_candidate(
		prepare_candidate, &context, PAYLOAD_MM_AUTHVAR_MODE_SETUP,
		&published_modes);
	if (success) {
		static const struct write_trace_entry golden_program[] = {
			{ 4129U, 39U, 0xffU }, { 4128U, 1U, 0xfeU },
			{ 4128U, 1U, 0xfcU }, { 4169U, 39U, 0xffU },
			{ 8192U, 4096U, 0U }, { 4168U, 1U, 0xfdU },
			{ 0U, 4096U, 0U }, { 4168U, 1U, 0xf9U },
			{ 4128U, 1U, 0xf8U },
		};
		static const struct write_trace_entry golden_erase[] = {
			{ 8192U, 4096U, 0U }, { 12288U, 4096U, 0U },
			{ 0U, 4096U, 0U }, { 8192U, 4096U, 0U },
			{ 12288U, 4096U, 0U },
		};

		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(published_modes == PAYLOAD_MM_AUTHVAR_MODE_SETUP);
		assert(program_count && erase_count && cache_ok() && !poisoned);
#ifndef EXECUTOR_REAL_MEDIA
		assert(program_count == ARRAY_SIZE(golden_program));
		assert(erase_count == ARRAY_SIZE(golden_erase));
		assert(!memcmp(program_trace, golden_program, sizeof(golden_program)));
		assert(!memcmp(erase_trace, golden_erase, sizeof(golden_erase)));
		assert(callback_trace_count == ARRAY_SIZE(candidate_callback_golden));
		assert(!memcmp(callback_trace, candidate_callback_golden,
			sizeof(candidate_callback_golden)));
#else
		(void)golden_program;
		(void)golden_erase;
#endif
	} else {
		assert(status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(published_modes == 0U && !cache_bound);
	}
}

static void candidate_admission_case(bool corrupt_policy)
{
	struct candidate_prepare_context context = {
		.mode = CANDIDATE_PREPARE_OK,
	};
	u8 published_modes = 0xffU;

	context.published_modes = &published_modes;
	make_candidate_source();
	if (corrupt_policy) {
		install();
		payload_mm_authvar_executor_test_corrupt_policy(true);
	}
	assert(payload_mm_authvar_executor_test_commit_candidate(
		prepare_candidate, &context, PAYLOAD_MM_AUTHVAR_MODE_SETUP,
		&published_modes) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(published_modes == 0U && begin_count == 0U && end_count == 0U &&
		read_count == 0U && program_count == 0U && erase_count == 0U &&
		operation_count == 0U && callback_trace_count == 0U &&
		cache_bind_count == 0U);
	if (corrupt_policy)
		payload_mm_authvar_executor_test_corrupt_policy(false);
	else
		install();
	assert(payload_mm_authvar_executor_test_commit_candidate(
		prepare_candidate, &context, PAYLOAD_MM_AUTHVAR_MODE_SETUP,
		&published_modes) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
}

static void candidate_media_error_case(enum candidate_media_fault_kind kind,
	enum payload_mm_authvar_media_result media_result, uint64_t expected_status)
{
	struct candidate_prepare_context context = {
		.mode = CANDIDATE_PREPARE_OK,
		.media_fault = kind,
		.media_fault_result = media_result,
	};
	u8 published_modes = 0xffU;
	uint64_t status;

	context.published_modes = &published_modes;
	make_candidate_source();
	install();
	status = payload_mm_authvar_executor_test_commit_candidate(
		prepare_candidate, &context, PAYLOAD_MM_AUTHVAR_MODE_SETUP,
		&published_modes);
	assert(status == expected_status);
	assert(published_modes == 0U && !cache_bound && cache_bind_count == 0U);
	assert(begin_count == 1U && end_count == 1U);
	assert(!candidate_media_fault_armed);
	if (kind == CANDIDATE_MEDIA_FAULT_READ)
		assert(program_count == 0U && erase_count == 0U);
}

static void candidate_result_bit_case(size_t offset, unsigned int bit)
{
	struct candidate_prepare_context context = {
		.mode = CANDIDATE_PREPARE_RESULT_BIT,
		.result_mutation_offset = offset,
		.result_mutation_bit = bit,
	};
	u8 published_modes = 0xffU;
	uint64_t status;

	context.published_modes = &published_modes;
	make_candidate_source();
	install();
	status = payload_mm_authvar_executor_test_commit_candidate(
		prepare_candidate, &context, PAYLOAD_MM_AUTHVAR_MODE_SETUP,
		&published_modes);
	assert(status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(published_modes == 0U && !cache_bound && cache_bind_count == 0U);
	assert(begin_count == 1U && end_count == 1U);
	assert(program_count == 0U && erase_count == 0U);
}

static void candidate_callback_fault_case(unsigned int fail_at,
	enum payload_mm_authvar_media_result media_result)
{
	struct candidate_prepare_context context = {
		.mode = CANDIDATE_PREPARE_OK,
	};
	u8 published_modes = 0xffU;
	uint64_t expected_status;
	uint64_t status;
	uint8_t target;

	context.published_modes = &published_modes;
	make_candidate_source();
	fail_operation = fail_at;
	generic_fault_result = media_result;
	install();
	status = payload_mm_authvar_executor_test_commit_candidate(
		prepare_candidate, &context, PAYLOAD_MM_AUTHVAR_MODE_SETUP,
		&published_modes);
	assert(fail_at && fail_at <= ARRAY_SIZE(candidate_callback_golden));
	assert(callback_trace_count >= fail_at);
	assert(!memcmp(callback_trace, candidate_callback_golden, fail_at));
	target = candidate_callback_golden[fail_at - 1U];
	expected_status = target == TEST_CALLBACK_END ?
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR :
		payload_mm_authvar_media_result_status(media_result);
	assert(status == expected_status && published_modes == 0U && !cache_bound);
	assert(cache_bind_count == (target == TEST_CALLBACK_END ? 1U : 0U));
	if (target == TEST_CALLBACK_BEGIN) {
		assert(callback_trace_count == fail_at && end_count == 0U);
	} else if (target == TEST_CALLBACK_END) {
		assert(callback_trace_count == fail_at && end_count == 1U);
	} else {
		assert(callback_trace_count == fail_at + 1U && end_count == 1U);
		assert(callback_trace[callback_trace_count - 1U] == TEST_CALLBACK_END);
	}
}

static unsigned int candidate_fault_number(const char *text)
{
	unsigned int value = 0;

	assert(*text);
	while (*text) {
		assert(*text >= '0' && *text <= '9');
		assert(value <= (UINT_MAX - (unsigned int)(*text - '0')) / 10U);
		value = value * 10U + (unsigned int)(*text++ - '0');
	}
	return value;
}

static void candidate_fault_pair(const char *text, unsigned int *first,
	unsigned int *second)
{
	const char *separator = strchr(text, '-');
	char number[32];
	size_t length;

	assert(separator);
	length = (size_t)(separator - text);
	assert(length && length < sizeof(number));
	memcpy(number, text, length);
	number[length] = 0;
	*first = candidate_fault_number(number);
	*second = candidate_fault_number(separator + 1U);
}

static bool candidate_reject_mode(const char *name,
	enum candidate_prepare_mode *mode)
{
	static const struct {
		const char *name;
		enum candidate_prepare_mode mode;
	} cases[] = {
		{ "generation", CANDIDATE_PREPARE_GENERATION },
		{ "token", CANDIDATE_PREPARE_TOKEN },
		{ "runtime", CANDIDATE_PREPARE_RUNTIME },
		{ "binding-reserved", CANDIDATE_PREPARE_BIND_RESERVED },
		{ "policy", CANDIDATE_PREPARE_POLICY },
		{ "source-used", CANDIDATE_PREPARE_SOURCE_USED },
		{ "candidate-used", CANDIDATE_PREPARE_CANDIDATE_USED },
		{ "count", CANDIDATE_PREPARE_COUNT },
		{ "modes", CANDIDATE_PREPARE_MODES },
		{ "mode-setup", CANDIDATE_PREPARE_MODE_SETUP },
		{ "mode-secure", CANDIDATE_PREPARE_MODE_SECURE },
		{ "mode-vendor", CANDIDATE_PREPARE_MODE_VENDOR },
		{ "result-reserved", CANDIDATE_PREPARE_RESULT_RESERVED },
		{ "source-digest", CANDIDATE_PREPARE_SOURCE_DIGEST },
		{ "candidate-digest", CANDIDATE_PREPARE_CANDIDATE_DIGEST },
		{ "candidate-body", CANDIDATE_PREPARE_CANDIDATE_BODY },
		{ "primary-media", CANDIDATE_PREPARE_PRIMARY_MEDIA },
		{ "working-media", CANDIDATE_PREPARE_WORKING_MEDIA },
		{ "queue-media", CANDIDATE_PREPARE_QUEUE_MEDIA },
		{ "spare-media", CANDIDATE_PREPARE_SPARE_MEDIA },
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		if (strcmp(name, cases[i].name))
			continue;
		*mode = cases[i].mode;
		return true;
	}
	return false;
}

#ifdef EXECUTOR_REAL_MEDIA
static void reset_candidate_commit(unsigned int cut, unsigned int recovery_cut,
	bool print_recovery_count)
{
	struct payload_mm_authvar_store_entry source_entries[64];
	struct payload_mm_authvar_store_entry scratch_entries[64];
	struct payload_mm_authvar_store_index source = {
		.entries = source_entries,
		.entry_capacity = ARRAY_SIZE(source_entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(source_entries),
	};
	const struct payload_mm_authvar_candidate_binding binding = {
		.generation = 1U,
		.token = 1U,
		.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP,
	};
	struct payload_mm_authvar_candidate_result result;
	struct candidate_prepare_context context = {
		.mode = CANDIDATE_PREPARE_OK,
	};
	uint8_t old_primary[BLOCK_SIZE];
	uint8_t new_primary[BLOCK_SIZE];
	uint8_t candidate[STORE_SIZE];
	int child_status;
	pid_t child;

	make_candidate_source();
	memcpy(old_primary, media, sizeof(old_primary));
	assert(payload_mm_authvar_store_scan(&source, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	assert(prepare_candidate(&source, &binding, candidate, sizeof(candidate),
		scratch_entries, ARRAY_SIZE(scratch_entries), &result, &context) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	memcpy(new_primary, old_primary, FV_HEADER_SIZE);
	memcpy(new_primary + FV_HEADER_SIZE, candidate, sizeof(candidate));
	child = fork();
	assert(child >= 0);
	if (!child) {
		u8 modes = 0;

		context.published_modes = &modes;
		reset_operation = cut;
		install();
		(void)payload_mm_authvar_executor_test_commit_candidate(
			prepare_candidate, &context, PAYLOAD_MM_AUTHVAR_MODE_SETUP,
			&modes);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child);
	assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 77);
	operation_count = 0;
	if (print_recovery_count) {
		char count[32];
		int length;

		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
		return;
	}
	if (recovery_cut) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			reset_operation = recovery_cut;
			install();
			(void)payload_mm_authvar_executor_recover();
			_exit(0);
		}
		assert(waitpid(child, &child_status, 0) == child);
		assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 77);
		operation_count = 0;
	}
	install();
	assert(payload_mm_authvar_executor_recover() ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(!memcmp(media, old_primary, sizeof(old_primary)) ||
		!memcmp(media, new_primary, sizeof(new_primary)));
	{
		struct payload_mm_authvar_ftw_plan plan;

		assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
			&plan) == CB_SUCCESS);
		assert(plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	}
	assert(erased(spare(), 2U * BLOCK_SIZE));
}
#endif
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
static const uint8_t coordinator_global_guid[16] = {
	0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11,
	0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c,
};
static const uint8_t coordinator_pkcs7_guid[16] = {
	0x9d, 0xd2, 0xaf, 0x4a, 0xdf, 0x68, 0xee, 0x49,
	0x8a, 0xa9, 0x34, 0x7d, 0x37, 0x56, 0x65, 0xa7,
};
static const uint8_t coordinator_pk_name[] = { 'P', 0, 'K', 0, 0, 0 };
static const uint8_t coordinator_kek_name[] = {
	'K', 0, 'E', 0, 'K', 0, 0, 0,
};
static const uint8_t coordinator_vendor_guid[16] = {
	0xe0, 0xe4, 0x73, 0x90, 0xec, 0x60, 0x6e, 0x4b,
	0x99, 0x03, 0x4c, 0x22, 0x3c, 0x26, 0x0f, 0x3c,
};
static const uint8_t coordinator_vendor_name[] = {
	'V', 0, 'e', 0, 'n', 0, 'd', 0, 'o', 0, 'r', 0, 'K', 0, 'e', 0,
	'y', 0, 's', 0, 'N', 0, 'v', 0, 0, 0,
};
static const uint8_t coordinator_enable_guid[16] = {
	0xc7, 0x0b, 0xa3, 0xf0, 0x08, 0xaf, 0x56, 0x45,
	0x99, 0xc4, 0x00, 0x10, 0x09, 0xc9, 0x3a, 0x44,
};
static const uint8_t coordinator_enable_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 'E', 0, 'n', 0, 'a', 0, 'b', 0, 'l', 0, 'e', 0,
	0, 0,
};
#ifdef EXECUTOR_REAL_MEDIA
static const uint8_t coordinator_secure_name[] = {
	'S', 0, 'e', 0, 'c', 0, 'u', 0, 'r', 0, 'e', 0, 'B', 0, 'o', 0,
	'o', 0, 't', 0, 0, 0,
};
#endif
static const uint8_t coordinator_custom_guid[16] = {
	0x0c, 0xec, 0x76, 0xc0, 0x28, 0x70, 0x99, 0x43,
	0xa0, 0x72, 0x71, 0xee, 0x5c, 0x44, 0x8b, 0x9f,
};
static const uint8_t coordinator_custom_name[] = {
	'C', 0, 'u', 0, 's', 0, 't', 0, 'o', 0, 'm', 0, 'M', 0, 'o', 0,
	'd', 0, 'e', 0, 0, 0,
};
static struct payload_mm_crypto_owner coordinator_owner;
static struct payload_mm_crypto_owner coordinator_alternate_owner;
static unsigned int coordinator_verify_calls;
static unsigned int coordinator_verify_operation;
static enum payload_mm_authvar_authority coordinator_accepted_authority =
	PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
static struct payload_mm_authvar_coordinator_test_request *coordinator_descriptor;
static struct payload_mm_authvar_coordinator_test_result *coordinator_result;
static struct payload_mm_authvar_policy_request *coordinator_policy_request;
static uint8_t *coordinator_context;

enum coordinator_attack {
	COORDINATOR_ATTACK_NONE,
	COORDINATOR_ATTACK_OWNER_ABANDON,
	COORDINATOR_ATTACK_ALTERNATE_OWNER_ABANDON,
	COORDINATOR_ATTACK_OWNER_DIRTY,
	COORDINATOR_ATTACK_DESCRIPTOR,
	COORDINATOR_ATTACK_REQUEST,
	COORDINATOR_ATTACK_NAME,
	COORDINATOR_ATTACK_DATA,
	COORDINATOR_ATTACK_CONTEXT,
	COORDINATOR_ATTACK_INTERNAL_CONTEXT,
	COORDINATOR_ATTACK_RESULT,
};

static enum coordinator_attack coordinator_attack;

static size_t coordinator_auth2(uint8_t *data, const void *payload,
	size_t payload_size)
{
	memset(data, 0, 41U + payload_size);
	put16(data, 2026U);
	data[2] = 9U;
	data[3] = 23U;
	data[6] = 1U;
	put32(data + 16U, 25U);
	put16(data + 20U, 0x0200U);
	put16(data + 22U, 0x0ef1U);
	memcpy(data + 24U, coordinator_pkcs7_guid,
		sizeof(coordinator_pkcs7_guid));
	data[40] = 0x30U;
	memcpy(data + 41U, payload, payload_size);
	return 41U + payload_size;
}

static enum payload_mm_verify_status coordinator_verify(void *context,
	const struct payload_mm_authvar_authority_verify_request *request,
	struct payload_mm_authvar_authority_verification *verification)
{
	assert(request->owner == &coordinator_owner);
	assert(coordinator_result->status ==
		PAYLOAD_MM_AUTHVAR_SERVICE_STATUS_PENDING &&
		coordinator_result->outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		coordinator_result->volatile_modes == 0U &&
		coordinator_result->completion == PAYLOAD_MM_AUTHVAR_SERVICE_PENDING);
	for (size_t i = 0U; i < sizeof(coordinator_result->reserved); i++)
		assert(coordinator_result->reserved[i] == 0U);
	coordinator_verify_calls++;
	coordinator_verify_operation = operation_count;
	assert(payload_mm_crypto_begin(&coordinator_owner) ==
		PAYLOAD_MM_VERIFY_OK);
	coordinator_owner.arena[0] = 0x5aU;
	coordinator_owner.arena_used = 1U;
	coordinator_owner.allocation_count = 1U;
	assert(payload_mm_crypto_end(&coordinator_owner,
		PAYLOAD_MM_VERIFY_OK) == PAYLOAD_MM_VERIFY_OK);
	switch (coordinator_attack) {
	case COORDINATOR_ATTACK_OWNER_ABANDON:
		assert(payload_mm_crypto_begin(&coordinator_owner) ==
			PAYLOAD_MM_VERIFY_OK);
		coordinator_owner.arena[0] = 0xa5U;
		coordinator_owner.arena_used = 1U;
		coordinator_owner.allocation_count = 1U;
		break;
	case COORDINATOR_ATTACK_ALTERNATE_OWNER_ABANDON:
		assert(payload_mm_crypto_begin(&coordinator_alternate_owner) ==
			PAYLOAD_MM_VERIFY_OK);
		coordinator_alternate_owner.arena[0] = 0xa5U;
		coordinator_alternate_owner.arena_used = 1U;
		coordinator_alternate_owner.allocation_count = 1U;
		break;
	case COORDINATOR_ATTACK_OWNER_DIRTY:
		coordinator_owner.arena[0] = 0xa5U;
		coordinator_owner.arena_used = 1U;
		break;
	case COORDINATOR_ATTACK_DESCRIPTOR:
		coordinator_descriptor->reserved[0] ^= 1U;
		break;
	case COORDINATOR_ATTACK_REQUEST:
		coordinator_policy_request->name_size ^= 2U;
		break;
	case COORDINATOR_ATTACK_NAME:
		((uint8_t *)(uintptr_t)coordinator_policy_request->name)[0] ^= 1U;
		break;
	case COORDINATOR_ATTACK_DATA:
		((uint8_t *)(uintptr_t)coordinator_policy_request->data)[0] ^= 1U;
		break;
	case COORDINATOR_ATTACK_CONTEXT:
		coordinator_context[0] ^= 1U;
		break;
	case COORDINATOR_ATTACK_INTERNAL_CONTEXT:
		((uint8_t *)context)[0] ^= 1U;
		break;
	case COORDINATOR_ATTACK_RESULT:
		coordinator_result->status ^= 1U;
		break;
	case COORDINATOR_ATTACK_NONE:
		break;
	}
	verification->accepted_authority = coordinator_accepted_authority;
	if (coordinator_accepted_authority ==
	    PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER &&
	    request->new_payload->size) {
		verification->new_binding_size = 32U;
		memset(verification->new_binding, 0xa5, 32U);
	}
	return coordinator_verify_status;
}

#if CONFIG(PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER)
enum payload_mm_verify_status payload_mm_authvar_private_trust_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	struct payload_mm_authvar_authority_verification *verification)
{
	const struct payload_mm_authvar_authority_verify_request request = {
		.owner = owner,
		.pkcs7 = signed_data,
		.content = content,
		.content_count = content_count,
		.index = index,
		.route = plan,
		.new_payload = &content[4],
	};

	return coordinator_verify(NULL, &request, verification);
}

enum payload_mm_verify_status payload_mm_authvar_trust_store_verify(
	struct payload_mm_crypto_owner *owner,
	const struct payload_mm_crypto_span *signed_data,
	const struct payload_mm_crypto_span *content, size_t content_count,
	const struct payload_mm_authvar_store_index *index,
	const struct payload_mm_authvar_route_plan *plan,
	enum payload_mm_authvar_authority *accepted_authority)
{
	struct payload_mm_authvar_authority_verification verification = { 0 };
	const struct payload_mm_authvar_authority_verify_request request = {
		.owner = owner,
		.pkcs7 = signed_data,
		.content = content,
		.content_count = content_count,
		.index = index,
		.route = plan,
		.new_payload = &content[4],
	};
	enum payload_mm_verify_status status =
		coordinator_verify(NULL, &request, &verification);

	if (status == PAYLOAD_MM_VERIFY_OK)
		*accepted_authority = verification.accepted_authority;
	return status;
}
#endif

struct coordinator_fixture {
	uint8_t auth2[42];
	uint8_t counter_auth[PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE];
	uint8_t payload;
	uint8_t kek_name[sizeof(coordinator_kek_name)]
		__aligned(__BIGGEST_ALIGNMENT__);
	uint8_t context[257];
	struct payload_mm_authvar_policy_request policy_request;
	struct payload_mm_authvar_coordinator_test_request request;
	struct payload_mm_authvar_coordinator_test_result result;
};

#ifdef EXECUTOR_REAL_MEDIA
static void coordinator_expected_candidate(
	const struct coordinator_fixture *fixture, uint8_t expected[BLOCK_SIZE])
{
	struct payload_mm_authvar_store_entry source_entries[64];
	struct payload_mm_authvar_store_index source = {
		.entries = source_entries,
		.entry_capacity = ARRAY_SIZE(source_entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(source_entries),
	};
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = fixture->kek_name,
		.name_size = sizeof(fixture->kek_name),
		.attributes = fixture->policy_request.attributes,
	};
	const struct payload_mm_authvar_record_span span = {
		.data = &fixture->payload,
		.size = sizeof(fixture->payload),
	};
	uint8_t candidate[STORE_SIZE];
	uint32_t record_size;

	assert(payload_mm_authvar_store_scan(&source, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	memset(candidate, 0xff, sizeof(candidate));
	memcpy(candidate, source.store, source.used_size);
	memcpy(descriptor.vendor_guid, fixture->policy_request.vendor_guid,
		sizeof(descriptor.vendor_guid));
	memcpy(descriptor.timestamp, fixture->auth2,
		sizeof(descriptor.timestamp));
	assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
		candidate + source.used_size, sizeof(candidate) - source.used_size,
		&record_size));
	candidate[source.used_size + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	memcpy(expected, media, FV_HEADER_SIZE);
	memcpy(expected + FV_HEADER_SIZE, candidate, sizeof(candidate));
}
#endif

static void coordinator_fixture_build(struct coordinator_fixture *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->payload = 0xa5U;
	memcpy(fixture->kek_name, coordinator_kek_name,
		sizeof(fixture->kek_name));
	fixture->context[0] = 0x3cU;
	fixture->policy_request = (struct payload_mm_authvar_policy_request) {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		.name = coordinator_pk_name,
		.name_size = sizeof(coordinator_pk_name),
		.data = fixture->auth2,
		.data_size = coordinator_auth2(fixture->auth2, &fixture->payload,
			sizeof(fixture->payload)),
	};
	fixture->request = (struct payload_mm_authvar_coordinator_test_request) {
		.revision = PAYLOAD_MM_AUTHVAR_COORDINATOR_TEST_REVISION,
		.size = sizeof(fixture->request),
		.request = &fixture->policy_request,
		.owner = &coordinator_owner,
		.verify = coordinator_verify,
		.verify_context = fixture->context,
		.verify_context_size = 1U,
	};
	memcpy(fixture->policy_request.vendor_guid, coordinator_global_guid,
		sizeof(coordinator_global_guid));
	memset(&coordinator_owner, 0, sizeof(coordinator_owner));
	memset(&coordinator_alternate_owner, 0,
		sizeof(coordinator_alternate_owner));
	coordinator_verify_status = PAYLOAD_MM_VERIFY_OK;
	coordinator_verify_calls = 0U;
	coordinator_verify_operation = 0U;
	coordinator_accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	coordinator_attack = COORDINATOR_ATTACK_NONE;
	coordinator_descriptor = &fixture->request;
	coordinator_result = &fixture->result;
	coordinator_policy_request = &fixture->policy_request;
	coordinator_context = fixture->context;
	coordinator_media_attack = COORDINATOR_MEDIA_ATTACK_NONE;
	coordinator_media_attack_callback = 0;
	coordinator_media_result = &fixture->result;
	coordinator_media_owner = &coordinator_owner;
	coordinator_media_alternate_owner = &coordinator_alternate_owner;
	coordinator_media_context = fixture->context;
}

static uint32_t coordinator_append_record(uint32_t offset,
	const uint8_t guid[16], const uint8_t *name, size_t name_size,
	uint32_t attributes, const uint8_t timestamp[16], const void *data,
	size_t data_size, enum payload_mm_authvar_record_timestamp time_state)
{
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = name,
		.name_size = name_size,
		.attributes = attributes,
	};
	const struct payload_mm_authvar_record_span span = {
		.data = data,
		.size = data_size,
	};
	uint32_t record_size;

	memcpy(descriptor.vendor_guid, guid, sizeof(descriptor.vendor_guid));
	memcpy(descriptor.timestamp, timestamp, sizeof(descriptor.timestamp));
	assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
		time_state, media + FV_HEADER_SIZE + offset, STORE_SIZE - offset,
		&record_size));
	media[FV_HEADER_SIZE + offset + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	return offset + record_size;
}

static void __maybe_unused native_inject_collision(void)
{
	static const uint8_t setup_name[] = {
		'S', 0, 'e', 0, 't', 0, 'u', 0, 'p', 0, 'M', 0, 'o', 0, 'd', 0,
		'e', 0, 0, 0,
	};
	static const uint8_t zero_timestamp[16];
	static const uint8_t one = 1U;
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(entries),
	};

	assert(payload_mm_authvar_store_scan(&index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	(void)coordinator_append_record(index.used_size, coordinator_global_guid,
		setup_name, sizeof(setup_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		zero_timestamp, &one, sizeof(one),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
}

static void coordinator_make_user_source(const struct coordinator_fixture *fixture,
	bool custom_mode)
{
	static const uint8_t zero_timestamp[16];
	static const uint8_t one = 1U;
	const uint32_t nv_bs = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	uint32_t offset = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;

	make_clean();
	offset = coordinator_append_record(offset, coordinator_vendor_guid,
		coordinator_vendor_name, sizeof(coordinator_vendor_name),
		nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		zero_timestamp, &one, sizeof(one),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO);
	offset = coordinator_append_record(offset, coordinator_global_guid,
		coordinator_pk_name, sizeof(coordinator_pk_name),
		nv_bs | PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		fixture->auth2, &fixture->payload, sizeof(fixture->payload),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	offset = coordinator_append_record(offset, coordinator_enable_guid,
		coordinator_enable_name, sizeof(coordinator_enable_name), nv_bs,
		zero_timestamp, &one, sizeof(one),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	if (custom_mode)
		(void)coordinator_append_record(offset, coordinator_custom_guid,
			coordinator_custom_name, sizeof(coordinator_custom_name), nv_bs,
			zero_timestamp, &one, sizeof(one),
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
}

static void coordinator_fixture_init(struct coordinator_fixture *fixture)
{
	coordinator_fixture_build(fixture);
	make_candidate_source();
	install();
	assert(payload_mm_authvar_executor_test_coordinate(&fixture->request,
		&fixture->result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(fixture->result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		fixture->result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		fixture->result.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT &&
		fixture->result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	assert(program_count && erase_count);
#ifndef EXECUTOR_REAL_MEDIA
	assert(cache_bound && cache_bind_count == 1U);
#endif
	assert(begin_count == 1U && end_count == 1U && !poisoned);
	assert(coordinator_verify_calls == 0U);
	fixture->policy_request.name = fixture->kek_name;
	fixture->policy_request.name_size = sizeof(coordinator_kek_name);
}

#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
static void fmp_prepare_modes(void)
{
	struct coordinator_fixture fixture;

	coordinator_fixture_build(&fixture);
	coordinator_make_user_source(&fixture, false);
}

static void generic_get(void)
{
	uint8_t data = 0U;
	struct payload_mm_authvar_read_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		.name = coordinator_vendor_name,
		.name_size = sizeof(coordinator_vendor_name),
		.result_data = &data,
		.data_capacity = sizeof(data),
	};
	struct payload_mm_authvar_read_result result;

	fmp_prepare_modes();
	install();
	memcpy(request.vendor_guid, coordinator_vendor_guid,
		sizeof(request.vendor_guid));
	memset(&result, 0xa5, sizeof(result));
	assert(payload_mm_authvar_read_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		result.required_data_size == sizeof(data) && data == 1U && !poisoned &&
		begin_count == 1U && end_count == 1U && !fmp_record_sha_calls);
}

static void generic_authvar_owner_output(bool partial)
{
	uint8_t before[256];
	uint8_t data;
	size_t owner_size;
	const uint8_t *owner =
		payload_mm_fmp_owner_authvar_test_storage(&owner_size);
	struct payload_mm_authvar_read_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		.name = coordinator_vendor_name,
		.name_size = sizeof(coordinator_vendor_name),
		.result_data = &data,
		.data_capacity = sizeof(data),
	};
	struct payload_mm_authvar_read_result *result;

	assert(owner && owner_size <= sizeof(before) && owner_size >= 8U);
	memcpy(before, owner, owner_size);
	result = (void *)(partial ? owner + owner_size - 8U : owner);
	memcpy(request.vendor_guid, coordinator_vendor_guid,
		sizeof(request.vendor_guid));
	assert(payload_mm_authvar_read_transaction(&request, result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!memcmp(before, owner, owner_size) && !begin_count && !end_count &&
		!program_count && !erase_count && !poisoned);
}
#endif

static void coordinator_success(void)
{
	struct coordinator_fixture fixture;

	coordinator_fixture_init(&fixture);
	memset(&fixture.result, 0xa5, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		fixture.result.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	assert(coordinator_verify_calls == 1U && cache_bind_count == 2U);
	assert(begin_count == 2U && end_count == 2U && !poisoned);
	assert(coordinator_owner.allocation_count == 1U &&
		payload_mm_crypto_idle());
}

static void coordinator_preflight_consumers(void)
{
	static const uint8_t ordinary_name[] = { 'Z', 0U, 0U, 0U };
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_policy_request lifecycle = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
	};
	struct payload_mm_authvar_policy_result result;
	unsigned int programs;
	unsigned int verifies;
	const uint32_t counter_attributes =
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_AUTHENTICATED_WRITE;

	coordinator_fixture_init(&fixture);
	fixture.policy_request.name = ordinary_name;
	fixture.policy_request.name_size = sizeof(ordinary_name);
	fixture.policy_request.attributes =
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	fixture.policy_request.data = NULL;
	fixture.policy_request.data_size = 0U;
	test_policy_authorize_count = 0U;
	programs = program_count;
	assert(payload_mm_authvar_policy_transaction(&fixture.policy_request,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		test_policy_authorize_count == 0U && program_count == programs);

	fixture.policy_request.attributes =
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	fixture.policy_request.data = fixture.auth2;
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
		&fixture.payload, sizeof(fixture.payload));
	assert(payload_mm_authvar_policy_transaction(&fixture.policy_request,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	assert(test_policy_authorize_count == 0U && program_count == programs);

	fixture.policy_request.attributes = counter_attributes;
	fixture.policy_request.data = fixture.counter_auth;
	fixture.policy_request.data_size = PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE - 1U;
	assert(payload_mm_authvar_policy_transaction(&fixture.policy_request,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	fixture.policy_request.data_size = PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE;
	assert(payload_mm_authvar_policy_transaction(&fixture.policy_request,
		&result) == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	assert(test_policy_authorize_count == 0U && program_count == programs);

	verifies = coordinator_verify_calls;
	fixture.policy_request.data_size = PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE - 1U;
	memset(&fixture.result, 0, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	fixture.policy_request.data_size = PAYLOAD_MM_AUTHVAR_COUNTER_AUTH_SIZE;
	memset(&fixture.result, 0, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
	assert(coordinator_verify_calls == verifies && program_count == programs &&
		!poisoned);

	fixture.policy_request.attributes =
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	fixture.policy_request.data = fixture.auth2;
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
		&fixture.payload, sizeof(fixture.payload));
	fixture.policy_request.attributes =
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;
	coordinator_accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	verifies = coordinator_verify_calls;
	memset(&fixture.result, 0, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND &&
		fixture.result.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		coordinator_verify_calls == verifies + 1U && program_count == programs &&
		!poisoned);

	fixture.policy_request.attributes =
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED;

	assert(payload_mm_authvar_policy_transaction(&lifecycle, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	coordinator_accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	verifies = coordinator_verify_calls;
	memset(&fixture.result, 0, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		coordinator_verify_calls == verifies + 1U && program_count == programs &&
		!poisoned);

	coordinator_accepted_authority = PAYLOAD_MM_AUTHVAR_AUTHORITY_CURRENT_PK;
	memset(&fixture.result, 0, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION &&
		coordinator_verify_calls == verifies + 2U && program_count == programs &&
		!poisoned);
}

static const struct payload_mm_authvar_store_entry *native_find(
	const uint8_t guid[16], const uint8_t *name, size_t name_size,
	struct payload_mm_authvar_store_index *index,
	struct payload_mm_authvar_store_entry *entries, size_t entry_count)
{
	struct payload_mm_authvar_store_limits limits;

	assert(entry_count <= UINT32_MAX);
	limits = (struct payload_mm_authvar_store_limits) {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = (uint32_t)entry_count,
	};

	memset(index, 0, sizeof(*index));
	index->entries = entries;
	index->entry_capacity = (uint32_t)entry_count;
	assert(payload_mm_authvar_store_scan(index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	return payload_mm_authvar_store_find(index, guid, name, name_size);
}

static void native_assert_value(const uint8_t guid[16], const uint8_t *name,
	size_t name_size, uint32_t attributes, const uint8_t *data,
	size_t data_size)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	const struct payload_mm_authvar_store_entry *entry;
	const uint8_t *stored;
	const uint8_t *timestamp;
	uint8_t combined = 0U;

	entry = native_find(guid, name, name_size, &index, entries,
		ARRAY_SIZE(entries));
	assert(entry && entry->attributes == attributes &&
		entry->data_size == data_size);
	stored = payload_mm_authvar_store_data(&index, entry);
	assert(stored && !memcmp(stored, data, data_size));
	timestamp = index.store + entry->record_offset + 16U;
	for (size_t i = 0U; i < 16U; i++)
		combined |= timestamp[i];
	assert(!combined);
}

static void coordinator_private_fixture_init(struct coordinator_fixture *fixture)
{
	static const uint8_t certdb_guid[16] = {
		0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
		0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
	};
	static const uint8_t certdb_name[] = {
		'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
	};
	static const uint8_t empty_certdb[] = { 4U, 0U, 0U, 0U };
	static const uint8_t zero_timestamp[16];
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;

	coordinator_fixture_build(fixture);
	coordinator_make_user_source(fixture, false);
	(void)native_find(certdb_guid, certdb_name, sizeof(certdb_name), &index,
		entries, ARRAY_SIZE(entries));
	assert(!payload_mm_authvar_store_find(&index, certdb_guid, certdb_name,
		sizeof(certdb_name)));
	(void)coordinator_append_record(index.used_size, certdb_guid, certdb_name,
		sizeof(certdb_name), 0x27U, zero_timestamp, empty_certdb,
		sizeof(empty_certdb), PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_TRUSTED_ZERO);
}

static void coordinator_private_atomic(bool concrete_provider)
{
	static const uint8_t guid[16] = { 0x42 };
	static const uint8_t name[] = { 'P', 0, 'r', 0, 'i', 0, 'v', 0, 0, 0 };
	static const uint8_t certdb_guid[16] = {
		0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
		0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
	};
	static const uint8_t certdb_name[] = {
		'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
	};
	static const uint8_t expected_binding[32] = { [0 ... 31] = 0xa5 };
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_certdb_binding certdb_binding;
	struct coordinator_fixture fixture;
	const struct payload_mm_authvar_store_entry *target;
	const struct payload_mm_authvar_store_entry *certdb;
	const void *certdb_data;
	uint64_t status;

	coordinator_private_fixture_init(&fixture);
#if CONFIG(PAYLOAD_MM_AUTHVAR_AUTHORITY_PROVIDER)
	if (concrete_provider) {
		fixture.request.verify =
			payload_mm_authvar_authority_provider_verify;
		fixture.request.verify_context = NULL;
		fixture.request.verify_context_size = 0U;
	}
#else
	assert(!concrete_provider);
#endif
	install();
	fixture.policy_request.name = name;
	fixture.policy_request.name_size = sizeof(name);
	memcpy(fixture.policy_request.vendor_guid, guid, sizeof(guid));
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
		&fixture.payload, sizeof(fixture.payload));
	coordinator_accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	memset(&fixture.result, 0, sizeof(fixture.result));
	status = payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result);
	assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	target = native_find(guid, name, sizeof(name), &index, entries,
		ARRAY_SIZE(entries));
	assert(target && target->data_size == sizeof(fixture.payload) &&
		*(const uint8_t *)payload_mm_authvar_store_data(&index, target) ==
			fixture.payload);
	certdb = payload_mm_authvar_store_find(&index, certdb_guid, certdb_name,
		sizeof(certdb_name));
	certdb_data = payload_mm_authvar_store_data(&index, certdb);
	assert(certdb && certdb_data && certdb->attributes == 0x27U &&
		payload_mm_authvar_certdb_find(certdb_data, certdb->data_size,
			guid, name, sizeof(name) - 2U, &certdb_binding) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_OK &&
		certdb_binding.size == sizeof(expected_binding) &&
		!memcmp(certdb_binding.data, expected_binding,
			sizeof(expected_binding)));

	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
		NULL, 0U);
	fixture.auth2[6] = 2U;
	coordinator_accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	memset(&fixture.result, 0, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	target = native_find(guid, name, sizeof(name), &index, entries,
		ARRAY_SIZE(entries));
	assert(!target);
	certdb = payload_mm_authvar_store_find(&index, certdb_guid, certdb_name,
		sizeof(certdb_name));
	certdb_data = payload_mm_authvar_store_data(&index, certdb);
	assert(certdb && certdb_data &&
		payload_mm_authvar_certdb_find(certdb_data, certdb->data_size,
			guid, name, sizeof(name) - 2U, &certdb_binding) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND);
}

static void coordinator_native_ordinary(void)
{
	static const uint8_t guid[16] = {
		0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	};
	static const uint8_t name[] = { 'O', 0, 'r', 0, 'd', 0, 0, 0 };
	static const uint8_t first[] = { 0x11, 0x22 };
	static const uint8_t suffix[] = { 0x33 };
	static const uint8_t appended[] = { 0x11, 0x22, 0x33 };
	static const uint8_t replacement[] = { 0x44 };
	static const uint8_t runtime_value[] = { 0x55 };
	static const uint8_t large_name[] = { 'L', 0, 'a', 0, 'r', 0, 'g', 0,
		'e', 0, 0, 0 };
	static const uint8_t reserved_name[] = {
		'S', 0, 'e', 0, 't', 0, 'u', 0, 'p', 0, 'M', 0, 'o', 0, 'd', 0,
		'e', 0, 0, 0,
	};
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = name,
		.name_size = sizeof(name),
		.data = first,
		.data_size = sizeof(first),
	};
	struct payload_mm_authvar_policy_result result;
	struct payload_mm_authvar_policy_request lifecycle = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
	};
	unsigned int programs;
	unsigned int erases;
	unsigned int begins;
	unsigned int ends;
	uint8_t maximum_data[2048];

	memcpy(request.vendor_guid, guid, sizeof(guid));
	make_candidate_source();
	coordinator_install_executor();
	test_policy_authorize_count = 0U;
	programs = program_count;
	erases = erase_count;
	begins = begin_count;
	ends = end_count;
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS;
	request.name = coordinator_enable_name;
	request.name_size = sizeof(coordinator_enable_name);
	request.data = first;
	request.data_size = 1U;
	memcpy(request.vendor_guid, coordinator_enable_guid,
		sizeof(request.vendor_guid));
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		program_count == programs && erase_count == erases &&
		begin_count == begins + 1U && end_count == ends + 1U && !poisoned);
	request.name = coordinator_custom_name;
	request.name_size = sizeof(coordinator_custom_name);
	memcpy(request.vendor_guid, coordinator_custom_guid,
		sizeof(request.vendor_guid));
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		program_count == programs && erase_count == erases &&
		begin_count == begins + 2U && end_count == ends + 2U && !poisoned);
	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	request.name = name;
	request.name_size = sizeof(name);
	request.data = first;
	request.data_size = sizeof(first);
	memcpy(request.vendor_guid, guid, sizeof(guid));
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	native_assert_value(guid, name, sizeof(name), request.attributes,
		first, sizeof(first));
	assert(test_policy_authorize_count == 0U && begin_count == begins + 3U &&
		end_count == ends + 3U && !poisoned);

	programs = program_count;
	erases = erase_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(program_count == programs && erase_count == erases);

	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	request.data = suffix;
	request.data_size = sizeof(suffix);
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	native_assert_value(guid, name, sizeof(name),
		request.attributes & ~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE,
		appended, sizeof(appended));

	programs = program_count;
	erases = erase_count;
	request.data = NULL;
	request.data_size = 0U;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(program_count == programs && erase_count == erases);

	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	request.data = replacement;
	request.data_size = sizeof(replacement);
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	native_assert_value(guid, name, sizeof(name), request.attributes,
		replacement, sizeof(replacement));

	request.attributes = 0U;
	request.data = first;
	request.data_size = sizeof(first);
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(!native_find(guid, name, sizeof(name), &index, entries,
		ARRAY_SIZE(entries)));

	programs = program_count;
	erases = erase_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(program_count == programs && erase_count == erases);

	request.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
		PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
		PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	request.data = NULL;
	request.data_size = 0U;
	begins = begin_count;
	ends = end_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(program_count == programs && erase_count == erases &&
		begin_count == begins + 1U && end_count == ends + 1U &&
		test_policy_authorize_count == 0U && !poisoned);

	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	request.data = replacement;
	request.data_size = sizeof(replacement);
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	request.attributes ^= PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	programs = program_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(program_count == programs);

	request.name = reserved_name;
	request.name_size = sizeof(reserved_name);
	memcpy(request.vendor_guid, coordinator_global_guid,
		sizeof(request.vendor_guid));
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
	assert(program_count == programs);

	request.name = name;
	request.name_size = sizeof(name);
	memcpy(request.vendor_guid, guid, sizeof(guid));
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	assert(payload_mm_authvar_policy_transaction(&lifecycle, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	request.data = runtime_value;
	request.data_size = sizeof(runtime_value);
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	native_assert_value(guid, name, sizeof(name), request.attributes,
		runtime_value, sizeof(runtime_value));

	memset(maximum_data, 0x6d, sizeof(maximum_data));
	request.name = large_name;
	request.name_size = sizeof(large_name);
	request.data = maximum_data;
	request.data_size = sizeof(maximum_data);
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	request.data = suffix;
	request.data_size = sizeof(suffix);
	programs = program_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(program_count == programs);

	request.name = (const uint8_t[]){ 'N', 0, 'e', 0, 'w', 0, 0, 0 };
	request.name_size = 8U;
	request.attributes &= ~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS;
	programs = program_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(program_count == programs && test_policy_authorize_count == 0U &&
		!poisoned);
}

static void coordinator_native_collision_precedence(void)
{
	static const uint8_t setup_name[] = {
		'S', 0, 'e', 0, 't', 0, 'u', 0, 'p', 0, 'M', 0, 'o', 0, 'd', 0,
		'e', 0, 0, 0,
	};
	static const uint8_t ordinary_name[] = { 'B', 0, 'a', 0, 'd', 0, 0, 0 };
	static const uint8_t zero_timestamp[16];
	static const uint8_t one = 1U;
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = ordinary_name,
		.name_size = sizeof(ordinary_name),
		.data = &one,
		.data_size = sizeof(one),
	};
	struct payload_mm_authvar_policy_result result;
	uint32_t offset;
	unsigned int programs;

	make_candidate_source();
	assert(native_find(coordinator_vendor_guid, coordinator_vendor_name,
		sizeof(coordinator_vendor_name), &index, entries,
		ARRAY_SIZE(entries)));
	offset = coordinator_append_record(index.used_size, coordinator_global_guid,
		setup_name, sizeof(setup_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS,
		zero_timestamp, &one, sizeof(one),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	assert(offset > index.used_size);
	memcpy(request.vendor_guid, coordinator_global_guid,
		sizeof(request.vendor_guid));
	coordinator_install_executor();
	programs = program_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		program_count == programs && test_policy_authorize_count == 0U &&
		poisoned);
}

static void coordinator_native_invalid(void)
{
	static const uint8_t name[] = { 'B', 0, 'a', 0, 'd', 0, 0, 0 };
	static const uint8_t one = 1U;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = name,
		.name_size = sizeof(name),
		.data = &one,
		.data_size = sizeof(one),
	};
	struct payload_mm_authvar_policy_result result;
	unsigned int programs;

	make_candidate_source();
	memcpy(request.vendor_guid, coordinator_global_guid,
		sizeof(request.vendor_guid));
	coordinator_install_executor();
	programs = program_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		program_count == programs && test_policy_authorize_count == 0U &&
		!poisoned);
}

static void coordinator_native_post_collision(void)
{
	static const uint8_t guid[16] = { 0x91 };
	static const uint8_t name[] = { 'P', 0, 'o', 0, 's', 0, 't', 0, 0, 0 };
	static const uint8_t data = 0x5aU;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = name,
		.name_size = sizeof(name),
		.data = &data,
		.data_size = sizeof(data),
	};
	struct payload_mm_authvar_policy_result result;

	memcpy(request.vendor_guid, guid, sizeof(guid));
	make_candidate_source();
	coordinator_install_executor();
	native_collision_program_count = program_count;
	native_collision_primary_reads = 0U;
	native_collision_on_read = true;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		program_count > native_collision_program_count &&
		!native_collision_on_read && poisoned);
}

static void coordinator_native_reclaim(void)
{
	static const uint8_t guid[16] = { 0x72, 0x63 };
	static const uint8_t target_name[] = { 'T', 0, 0, 0 };
	static const uint8_t zero_timestamp[16];
	uint8_t old_data[100];
	uint8_t new_data[108];
	uint8_t filler_data[100];
	uint8_t filler_name[] = { 'a', 0, 0, 0 };
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = target_name,
		.name_size = sizeof(target_name),
		.data = new_data,
		.data_size = sizeof(new_data),
	};
	struct payload_mm_authvar_policy_result result;
	size_t filler_record_size;
	size_t filler_data_offset;
	size_t new_record_size;
	size_t new_data_offset;
	uint32_t offset;
	unsigned int erases;

	memset(old_data, 0x31, sizeof(old_data));
	memset(new_data, 0x42, sizeof(new_data));
	memset(filler_data, 0x53, sizeof(filler_data));
	memcpy(request.vendor_guid, guid, sizeof(guid));
	make_candidate_source();
	assert(native_find(coordinator_vendor_guid, coordinator_vendor_name,
		sizeof(coordinator_vendor_name), &index, entries,
		ARRAY_SIZE(entries)));
	offset = coordinator_append_record(index.used_size, guid, target_name,
		sizeof(target_name), request.attributes, zero_timestamp, old_data,
		sizeof(old_data), PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	assert(payload_mm_authvar_record_layout(sizeof(filler_name),
		sizeof(filler_data), &filler_record_size, &filler_data_offset));
	assert(payload_mm_authvar_record_layout(sizeof(target_name),
		sizeof(new_data), &new_record_size, &new_data_offset));
	while (STORE_SIZE - offset >= filler_record_size +
	       (new_record_size - filler_record_size)) {
		offset = coordinator_append_record(offset, guid, filler_name,
			sizeof(filler_name), request.attributes, zero_timestamp,
			filler_data, sizeof(filler_data),
			PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
		filler_name[0]++;
		assert(filler_name[0] != 'T');
	}
	assert(STORE_SIZE - offset < new_record_size);
	coordinator_install_executor();
	erases = erase_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(erase_count > erases && test_policy_authorize_count == 0U &&
		!poisoned);
	native_assert_value(guid, target_name, sizeof(target_name),
		request.attributes, new_data, sizeof(new_data));
}

static void coordinator_native_copy_isolation(void)
{
	static const uint8_t guid[16] = { 0x36, 0x14 };
	uint8_t name[] = { 'C', 0, 'o', 0, 'p', 0, 'y', 0, 0, 0 };
	uint8_t data[] = { 0x12, 0x34 };
	uint8_t admitted_name[sizeof(name)];
	uint8_t admitted_data[sizeof(data)];
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = name,
		.name_size = sizeof(name),
		.data = data,
		.data_size = sizeof(data),
	};
	struct payload_mm_authvar_policy_result result;
	uint32_t admitted_attributes = request.attributes;

	memcpy(request.vendor_guid, guid, sizeof(guid));
	memcpy(admitted_name, name, sizeof(name));
	memcpy(admitted_data, data, sizeof(data));
	make_candidate_source();
	coordinator_install_executor();
	native_external_request = &request;
	native_external_name = name;
	native_external_data = data;
	native_mutate_on_begin = true;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(!native_mutate_on_begin && !poisoned);
	native_assert_value(guid, admitted_name, sizeof(admitted_name),
		admitted_attributes, admitted_data, sizeof(admitted_data));
}

static void coordinator_status_case(enum payload_mm_verify_status verify_status,
	uint64_t expected_status, bool expected_poison)
{
	struct coordinator_fixture fixture;
	unsigned int programs;
	unsigned int erases;
	unsigned int binds;

	coordinator_fixture_init(&fixture);
	coordinator_verify_status = verify_status;
	programs = program_count;
	erases = erase_count;
	binds = cache_bind_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == expected_status);
	assert(fixture.result.status == expected_status &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	assert(coordinator_verify_calls == 1U);
	assert(poisoned == expected_poison);
	if (verify_status == PAYLOAD_MM_VERIFY_OK) {
		assert(program_count > programs && erase_count > erases &&
			cache_bind_count == binds + 1U);
		assert(fixture.result.outcome ==
			PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION);
	} else {
		assert(program_count == programs && erase_count == erases &&
			cache_bind_count == binds);
		assert(fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
			fixture.result.volatile_modes == 0U);
	}
	assert(payload_mm_crypto_idle() && !coordinator_owner.busy &&
		!coordinator_owner.arena_used);
}

static void coordinator_no_write_case(bool append)
{
	struct coordinator_fixture fixture;
	unsigned int programs;
	unsigned int erases;
	unsigned int begins;
	unsigned int ends;
	unsigned int binds;
	uint64_t expected_status = append ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND;
	uint32_t expected_outcome = append ? PAYLOAD_MM_AUTHVAR_OUTCOME_NOOP :
		PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND;

	coordinator_fixture_init(&fixture);
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2, NULL, 0U);
	if (append)
		fixture.policy_request.attributes |= PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE;
	programs = program_count;
	erases = erase_count;
	binds = cache_bind_count;
	begins = begin_count;
	ends = end_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == expected_status);
	assert(fixture.result.status == expected_status &&
		fixture.result.outcome == expected_outcome &&
		fixture.result.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	assert(program_count == programs && erase_count == erases &&
		cache_bind_count == binds && !cache_bound);
	assert(begin_count == begins + 1U && end_count == ends + 1U &&
		coordinator_verify_calls == 1U && !poisoned);
}

static void coordinator_assert_committed_original(void)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_entry *entry;
	const uint8_t *data;

	assert(payload_mm_authvar_store_scan(&index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	entry = payload_mm_authvar_store_find(&index, coordinator_global_guid,
		coordinator_kek_name, sizeof(coordinator_kek_name));
	assert(entry && entry->data_size == 1U);
	data = payload_mm_authvar_store_data(&index, entry);
	assert(data && data[0] == 0xa5U);
}

static void coordinator_attack_case(enum coordinator_attack attack,
	bool copied_bytes_may_change)
{
	struct coordinator_fixture fixture;
	unsigned int programs;
	unsigned int erases;
	unsigned int begins;
	unsigned int ends;
	unsigned int binds;
	uint64_t expected_status = copied_bytes_may_change ?
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;

	coordinator_fixture_init(&fixture);
	coordinator_attack = attack;
	programs = program_count;
	erases = erase_count;
	binds = cache_bind_count;
	begins = begin_count;
	ends = end_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == expected_status);
	assert(fixture.result.status == expected_status &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	assert(coordinator_verify_calls == 1U && payload_mm_crypto_idle());
	assert(!coordinator_owner.busy && !coordinator_owner.arena_used &&
		!coordinator_alternate_owner.busy &&
		!coordinator_alternate_owner.arena_used);
	if (copied_bytes_may_change) {
		assert(!poisoned && program_count > programs && erase_count > erases &&
			cache_bind_count == binds + 1U &&
			fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION);
		coordinator_assert_committed_original();
	} else {
		assert(poisoned && program_count == programs && erase_count == erases &&
			cache_bind_count == binds && !cache_bound &&
			fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
			fixture.result.volatile_modes == 0U);
	}
	assert(begin_count == begins + 1U && end_count == ends + 1U);
	assert(payload_mm_crypto_begin(&coordinator_alternate_owner) ==
		PAYLOAD_MM_VERIFY_OK);
	assert(payload_mm_crypto_end(&coordinator_alternate_owner,
		PAYLOAD_MM_VERIFY_OK) == PAYLOAD_MM_VERIFY_OK);
}

static void coordinator_context_boundary(bool accepted)
{
	struct coordinator_fixture fixture;
	unsigned int begins;
	unsigned int programs;

	coordinator_fixture_init(&fixture);
	fixture.request.verify_context_size = accepted ? 256U : 257U;
	begins = begin_count;
	programs = program_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == (accepted ? PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER));
	if (accepted) {
		assert(coordinator_verify_calls == 1U && begin_count == begins + 1U &&
			program_count > programs);
	} else {
		assert(coordinator_verify_calls == 0U && begin_count == begins &&
			program_count == programs);
	}
}

static void coordinator_runtime_pk_delete(void)
{
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_policy_request lifecycle = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
	};
	struct payload_mm_authvar_policy_result lifecycle_result;
	uint8_t expected_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;

	coordinator_fixture_init(&fixture);
	assert(payload_mm_authvar_policy_transaction(&lifecycle,
		&lifecycle_result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(lifecycle_result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		lifecycle_result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	fixture.policy_request.name = coordinator_pk_name;
	fixture.policy_request.name_size = sizeof(coordinator_pk_name);
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2, NULL, 0U);
	fixture.auth2[6] = 2U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		fixture.result.volatile_modes == expected_modes && !poisoned);
	memset(&fixture.result, 0, sizeof(fixture.result));
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(fixture.kek_name);
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2, NULL, 0U);
	fixture.auth2[6] = 3U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND &&
		fixture.result.volatile_modes == expected_modes &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		!poisoned);
	/* SetupMode bypass applies after runtime PK deletion. */
	assert(coordinator_verify_calls == 1U);
}

static void coordinator_runtime_missing_enable(void)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(entries),
	};
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_policy_request lifecycle = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
	};
	struct payload_mm_authvar_policy_result lifecycle_result;
	const struct payload_mm_authvar_store_entry *enable;
	unsigned int programs;

	coordinator_fixture_init(&fixture);
	assert(payload_mm_authvar_policy_transaction(&lifecycle,
		&lifecycle_result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(payload_mm_authvar_store_scan(&index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	enable = payload_mm_authvar_mode_find(&index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE);
	assert(enable);
	media[FV_HEADER_SIZE + enable->record_offset + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
	programs = program_count;
	coordinator_verify_calls = 0U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		coordinator_verify_calls == 0U && program_count == programs && poisoned);
}

static uint8_t coordinator_vendor_value(void)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(entries),
	};
	uint8_t value;

	assert(payload_mm_authvar_store_scan(&index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	assert(payload_mm_authvar_mode_value(&index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_VENDOR_KEYS_NV,
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED, &value));
	return value;
}

static void coordinator_vendor_reconcile(void)
{
	struct coordinator_fixture fixture;

	coordinator_fixture_build(&fixture);
	coordinator_make_user_source(&fixture, true);
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(fixture.kek_name);
	fixture.request.trusted_physical_presence = 1U;
	install();
	assert(coordinator_vendor_value() == 1U);
	fail_end = true;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		coordinator_verify_calls == 0U && !poisoned && !cache_bound &&
		coordinator_vendor_value() == 0U);
	fail_end = false;
	fixture.payload = 0x5aU;
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
		&fixture.payload, sizeof(fixture.payload));
	fixture.auth2[6] = 2U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		fixture.result.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT &&
		coordinator_verify_calls == 0U && coordinator_vendor_value() == 0U &&
		!poisoned);
}

static void coordinator_nonboolean_modes(void)
{
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_entry *entry;
	struct coordinator_fixture fixture;
	uint8_t source_modes;

	coordinator_fixture_build(&fixture);
	coordinator_make_user_source(&fixture, true);
	assert(payload_mm_authvar_store_scan(&index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	entry = payload_mm_authvar_mode_find(&index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_SECURE_BOOT_ENABLE);
	assert(entry);
	media[FV_HEADER_SIZE + entry->data_offset] = 2U;
	assert(payload_mm_authvar_coordinator_source_modes(&index, false, false,
		0U, &source_modes));
	assert(source_modes == PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS);

	coordinator_make_user_source(&fixture, true);
	assert(payload_mm_authvar_store_scan(&index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	entry = payload_mm_authvar_mode_find(&index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_CUSTOM_MODE);
	assert(entry);
	media[FV_HEADER_SIZE + entry->data_offset] = 2U;
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(coordinator_kek_name);
	fixture.request.trusted_physical_presence = 1U;
	coordinator_verify_status = PAYLOAD_MM_VERIFY_REJECTED;
	install();
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION);
	assert(coordinator_verify_calls == 1U && !poisoned);
}

static void coordinator_native_reconcile(void)
{
	static const uint8_t name[] = { 'N', 0, 'o', 0, 'o', 0, 'p', 0, 0, 0 };
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_APPEND_WRITE,
		.name = name,
		.name_size = sizeof(name),
	};
	struct payload_mm_authvar_policy_result result;
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index = {
		.entries = entries,
		.entry_capacity = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(entries),
	};
	const struct payload_mm_authvar_store_entry *entry;
	unsigned int programs;
	uint32_t offset;

	coordinator_fixture_build(&fixture);
	coordinator_make_user_source(&fixture, true);
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(fixture.kek_name);
	fixture.request.trusted_physical_presence = 1U;
	memcpy(request.vendor_guid, fixture.policy_request.vendor_guid,
		sizeof(request.vendor_guid));
	install();
	fail_end = true;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(coordinator_vendor_value() == 0U && !poisoned);
	programs = program_count;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(program_count == programs && !poisoned);
	fail_end = false;
	assert(payload_mm_authvar_store_scan(&index, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	entry = payload_mm_authvar_mode_find(&index,
		PAYLOAD_MM_AUTHVAR_MODE_KEY_PK);
	assert(entry);
	media[FV_HEADER_SIZE + entry->record_offset + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED_DELETED;
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(program_count == programs && !poisoned);
	assert(native_find(coordinator_vendor_guid, coordinator_vendor_name,
		sizeof(coordinator_vendor_name), &index, entries,
		ARRAY_SIZE(entries)));
	offset = coordinator_append_record(index.used_size, coordinator_global_guid,
		coordinator_pk_name, sizeof(coordinator_pk_name),
		PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		fixture.auth2, &fixture.payload, sizeof(fixture.payload),
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED);
	assert(offset > index.used_size);
	assert(payload_mm_authvar_policy_transaction(&request, &result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(program_count == programs && poisoned);
}

static void coordinator_end_failure_recovery(void)
{
	struct coordinator_fixture fixture;

	memset(&fixture, 0, sizeof(fixture));
	fixture.payload = 0xa5U;
	fixture.context[0] = 0x3cU;
	memcpy(fixture.kek_name, coordinator_kek_name,
		sizeof(fixture.kek_name));
	fixture.policy_request = (struct payload_mm_authvar_policy_request) {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_TIME_AUTHENTICATED,
		.name = coordinator_pk_name,
		.name_size = sizeof(coordinator_pk_name),
		.data = fixture.auth2,
		.data_size = coordinator_auth2(fixture.auth2, &fixture.payload, 1U),
	};
	memcpy(fixture.policy_request.vendor_guid, coordinator_global_guid, 16U);
	fixture.request = (struct payload_mm_authvar_coordinator_test_request) {
		.revision = PAYLOAD_MM_AUTHVAR_COORDINATOR_TEST_REVISION,
		.size = sizeof(fixture.request),
		.request = &fixture.policy_request,
		.owner = &coordinator_owner,
		.verify = coordinator_verify,
		.verify_context = fixture.context,
		.verify_context_size = 1U,
	};
	memset(&coordinator_owner, 0, sizeof(coordinator_owner));
	memset(&coordinator_alternate_owner, 0,
		sizeof(coordinator_alternate_owner));
	coordinator_verify_status = PAYLOAD_MM_VERIFY_OK;
	coordinator_verify_calls = 0U;
	coordinator_attack = COORDINATOR_ATTACK_NONE;
	coordinator_descriptor = &fixture.request;
	coordinator_result = &fixture.result;
	coordinator_policy_request = &fixture.policy_request;
	coordinator_context = fixture.context;
	make_candidate_source();
	install();
	fail_end = true;
	generic_fault_result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		!cache_bound && !poisoned);
	assert(begin_count == 1U && end_count == 1U && program_count && erase_count);
	fail_end = false;
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(fixture.kek_name);
	memset(&fixture.result, 0, sizeof(fixture.result));
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		fixture.result.volatile_modes == PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT &&
		!poisoned);
	assert(begin_count == 2U && end_count == 2U);
}

static void coordinator_runtime_end_failure(void)
{
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_policy_request lifecycle = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_ENTER_RUNTIME,
	};
	struct payload_mm_authvar_policy_result lifecycle_result;
	uint8_t expected_modes = PAYLOAD_MM_AUTHVAR_MODE_SETUP |
		PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT;

	coordinator_fixture_init(&fixture);
	assert(payload_mm_authvar_policy_transaction(&lifecycle,
		&lifecycle_result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	fixture.policy_request.name = coordinator_pk_name;
	fixture.policy_request.name_size = sizeof(coordinator_pk_name);
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2, NULL, 0U);
	fixture.auth2[6] = 2U;
	fail_end = true;
	generic_fault_result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		!cache_bound && !poisoned);
	fail_end = false;
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(fixture.kek_name);
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2, NULL, 0U);
	fixture.auth2[6] = 3U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
	assert(fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NOT_FOUND &&
		fixture.result.volatile_modes == expected_modes && !poisoned);
}

static void coordinator_preexisting_crypto_owner(void)
{
	struct coordinator_fixture fixture;
	unsigned int begins;
	unsigned int programs;

	coordinator_fixture_init(&fixture);
	assert(payload_mm_crypto_begin(&coordinator_alternate_owner) ==
		PAYLOAD_MM_VERIFY_OK);
	coordinator_alternate_owner.arena[0] = 0xa5U;
	coordinator_alternate_owner.arena_used = 1U;
	coordinator_alternate_owner.allocation_count = 1U;
	begins = begin_count;
	programs = program_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		begin_count == begins && program_count == programs && !poisoned);
	assert(coordinator_alternate_owner.busy &&
		coordinator_alternate_owner.arena_used == 1U &&
		!payload_mm_crypto_idle());
	assert(payload_mm_crypto_end(&coordinator_alternate_owner,
		PAYLOAD_MM_VERIFY_OK) == PAYLOAD_MM_VERIFY_OK &&
		payload_mm_crypto_idle());
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
}

static void coordinator_admission_matrix(void)
{
	static union {
		struct payload_mm_crypto_owner alignment;
		uint8_t bytes[sizeof(struct payload_mm_crypto_owner) + 64U];
	} overlap;
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_coordinator_test_request base_request;
	struct payload_mm_authvar_policy_request base_policy;
	struct payload_mm_authvar_coordinator_test_result untouched;
	struct payload_mm_authvar_coordinator_test_result *result;
	unsigned int begins;
	unsigned int programs;

	coordinator_fixture_init(&fixture);
	base_request = fixture.request;
	base_policy = fixture.policy_request;
	memset(&untouched, 0xa5, sizeof(untouched));
	for (unsigned int test = 0U; test < 20U; test++) {
		fixture.request = base_request;
		fixture.policy_request = base_policy;
		memcpy(&fixture.result, &untouched, sizeof(untouched));
		result = &fixture.result;
		switch (test) {
		case 0U:
			fixture.request.revision++;
			break;
		case 1U:
			fixture.request.size--;
			break;
		case 2U:
			fixture.request.reserved[0] = 1U;
			break;
		case 3U:
			fixture.request.trusted_physical_presence = 2U;
			break;
		case 4U:
			fixture.request.verify_context = NULL;
			break;
		case 5U:
			fixture.request.owner =
				(struct payload_mm_crypto_owner *)((uint8_t *)&coordinator_owner + 1U);
			break;
		case 6U:
			fixture.request.request =
				(const struct payload_mm_authvar_policy_request *)
				((const uint8_t *)&fixture.policy_request + 1U);
			break;
		case 7U:
			result = (struct payload_mm_authvar_coordinator_test_result *)
				(void *)&fixture.request;
			break;
		case 8U:
			fixture.policy_request.data = fixture.policy_request.name;
			fixture.policy_request.data_size = 1U;
			break;
		case 9U:
			fixture.request.verify_context = &coordinator_owner;
			fixture.request.verify_context_size = 1U;
			break;
		case 10U:
			fixture.policy_request.name = NULL;
			fixture.policy_request.name_size = 0U;
			break;
		case 11U:
			fixture.policy_request.data = NULL;
			fixture.policy_request.data_size = 0U;
			break;
		case 12U:
			fixture.request.verify_context_size = 257U;
			break;
		case 13U:
			fixture.request.request = NULL;
			break;
		case 14U:
			fixture.request.owner = NULL;
			break;
		case 15U:
			fixture.request.verify = NULL;
			break;
		case 16U:
			fixture.request.verify_context_size = 0U;
			break;
		case 17U:
			fixture.policy_request.name_size = 129U;
			break;
		case 18U:
			fixture.policy_request.data_size = 2049U;
			break;
		case 19U:
			fixture.policy_request.name =
				(const void *)(uintptr_t)(UINTPTR_MAX - 1U);
			fixture.policy_request.name_size = 4U;
			break;
		}
		begins = begin_count;
		programs = program_count;
		assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
			result) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
		assert(begin_count == begins && program_count == programs &&
			coordinator_verify_calls == 0U && !poisoned);
		if (result == &fixture.result)
			assert(!memcmp(&fixture.result, &untouched, sizeof(untouched)));
	}
	for (unsigned int left = 0U; left < 7U; left++) {
		for (unsigned int right = left + 1U; right < 7U; right++) {
			uint8_t spans[14];
			const void *parts[7];
			size_t sizes[7];

			for (size_t part = 0U; part < 7U; part++) {
				parts[part] = &spans[part * 2U];
				sizes[part] = 1U;
			}
			parts[right] = parts[left];
			assert(!payload_mm_authvar_executor_test_coordinator_spans(parts,
				sizes));
		}
	}
	{
		uint8_t spans[14];
		const void *parts[7];
		size_t sizes[7];

		for (size_t part = 0U; part < 7U; part++) {
			parts[part] = &spans[part * 2U];
			sizes[part] = 2U;
		}
		assert(payload_mm_authvar_executor_test_coordinator_spans(parts, sizes));
	}
	assert(payload_mm_authvar_executor_test_coordinator_lengths(128U, 2048U,
		256U));
	assert(payload_mm_authvar_executor_test_coordinator_lengths(128U, 2048U,
		0U));
	assert(!payload_mm_authvar_executor_test_coordinator_lengths(129U, 2048U,
		256U));
	assert(!payload_mm_authvar_executor_test_coordinator_lengths(128U, 2049U,
		256U));
	assert(!payload_mm_authvar_executor_test_coordinator_lengths(128U, 2048U,
		257U));
	memcpy(overlap.bytes + 1U, &base_request, sizeof(base_request));
	memcpy(&fixture.result, &untouched, sizeof(untouched));
	begins = begin_count;
	assert(payload_mm_authvar_executor_test_coordinate(
		(const struct payload_mm_authvar_coordinator_test_request *)
		(void *)(overlap.bytes + 1U), &fixture.result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(begin_count == begins &&
		!memcmp(&fixture.result, &untouched, sizeof(untouched)));
	memset(overlap.bytes + 1U, 0xa5, sizeof(fixture.result));
	begins = begin_count;
	assert(payload_mm_authvar_executor_test_coordinate(&base_request,
		(struct payload_mm_authvar_coordinator_test_result *)
		(void *)(overlap.bytes + 1U)) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(begin_count == begins);
	memcpy(&fixture.result, &untouched, sizeof(untouched));
	assert(payload_mm_authvar_executor_test_coordinate(NULL,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!memcmp(&fixture.result, &untouched, sizeof(untouched)));
	assert(payload_mm_authvar_executor_test_coordinate(&base_request, NULL) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	fixture.request = base_request;
	fixture.policy_request = base_policy;
	fixture.request.verify_context = NULL;
	fixture.request.verify_context_size = 0U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(coordinator_verify_calls == 1U && !poisoned);
}

static void coordinator_media_attack_case(enum coordinator_media_attack attack,
	enum test_callback_kind callback)
{
	struct coordinator_fixture fixture;
	unsigned int begins;
	unsigned int ends;

	coordinator_fixture_init(&fixture);
	coordinator_media_attack = attack;
	coordinator_media_attack_callback = callback;
	begins = begin_count;
	ends = end_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		poisoned && !cache_bound && payload_mm_crypto_idle());
	assert(!coordinator_owner.busy && !coordinator_owner.arena_used &&
		!coordinator_alternate_owner.busy &&
		!coordinator_alternate_owner.arena_used);
	assert(begin_count == begins + 1U && end_count == ends + 1U);
	begins = begin_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(begin_count == begins &&
		fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
}

static void coordinator_callback_trace(void)
{
	struct coordinator_fixture fixture;

	coordinator_fixture_init(&fixture);
	operation_count = 0U;
	callback_trace_count = 0U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(callback_trace_count == ARRAY_SIZE(candidate_callback_golden) &&
		!memcmp(callback_trace, candidate_callback_golden,
			sizeof(candidate_callback_golden)));
}

static void coordinator_begin_attack_case(enum coordinator_media_attack attack)
{
	struct coordinator_fixture fixture;
	unsigned int ends;
	unsigned int programs;

	coordinator_fixture_init(&fixture);
	coordinator_media_attack = attack;
	coordinator_media_attack_callback = TEST_CALLBACK_BEGIN;
	operation_count = 0U;
	fail_operation = 1U;
	generic_fault_result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	ends = end_count;
	programs = program_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		end_count == ends && program_count == programs && poisoned &&
		payload_mm_crypto_idle() && !coordinator_owner.busy &&
		!coordinator_alternate_owner.busy);
}

static void coordinator_callback_fault(unsigned int fail_at,
	enum payload_mm_authvar_media_result media_failure)
{
	struct coordinator_fixture fixture;
	uint64_t expected_status;
	uint8_t target;

	assert(fail_at && fail_at <= ARRAY_SIZE(candidate_callback_golden));
	coordinator_fixture_init(&fixture);
	operation_count = 0U;
	callback_trace_count = 0U;
	fail_operation = fail_at;
	generic_fault_result = media_failure;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	target = candidate_callback_golden[fail_at - 1U];
	expected_status = target == TEST_CALLBACK_END ?
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR :
		payload_mm_authvar_media_result_status(media_failure);
	assert(fixture.result.status == expected_status &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	if (target != TEST_CALLBACK_BEGIN)
		assert(!cache_bound);
	assert(callback_trace_count >= fail_at &&
		!memcmp(callback_trace, candidate_callback_golden, fail_at));
	if (target == TEST_CALLBACK_BEGIN)
		assert(callback_trace_count == fail_at);
	else if (target == TEST_CALLBACK_END)
		assert(callback_trace_count == fail_at);
	else
		assert(callback_trace_count == fail_at + 1U &&
			callback_trace[callback_trace_count - 1U] == TEST_CALLBACK_END);
}

static void coordinator_forced_fault(
	enum payload_mm_authvar_coordinator_test_fault fault,
	uint64_t expected_status, bool expected_poison)
{
	struct coordinator_fixture fixture;
	unsigned int programs;
	unsigned int erases;
	unsigned int binds;

	coordinator_fixture_init(&fixture);
	payload_mm_authvar_coordinator_test_set_fault(fault);
	programs = program_count;
	erases = erase_count;
	binds = cache_bind_count;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == expected_status);
	assert(fixture.result.status == expected_status &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.volatile_modes == 0U &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		program_count == programs && erase_count == erases &&
		cache_bind_count == binds && poisoned == expected_poison &&
		coordinator_verify_calls == 1U && payload_mm_crypto_idle());
}

static void coordinator_policy_race(void)
{
	struct coordinator_fixture fixture;
	unsigned int begins;
	unsigned int ends;
	unsigned int reads;
	unsigned int programs;
	unsigned int operations;
	unsigned int binds;

	coordinator_fixture_init(&fixture);
	begins = begin_count;
	ends = end_count;
	reads = read_count;
	programs = program_count;
	operations = operation_count;
	binds = cache_bind_count;
	payload_mm_authvar_executor_test_corrupt_coordinate_policy();
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE &&
		fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		begin_count == begins && end_count == ends && read_count == reads &&
		program_count == programs && operation_count == operations &&
		cache_bind_count == binds);
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
}

static void coordinator_output_alias(void)
{
	struct alias_span {
		void *pointer;
		size_t size;
	};
	struct payload_mm_authvar_store_entry source_entries[64];
	struct payload_mm_authvar_store_entry candidate_entries[64];
	struct payload_mm_authvar_store_index source = {
		.entries = source_entries,
		.entry_capacity = ARRAY_SIZE(source_entries),
	};
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = STORE_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = ARRAY_SIZE(source_entries),
	};
	const struct payload_mm_authvar_write_policy policy = {
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_record_size = STORE_SIZE,
		.maximum_records = ARRAY_SIZE(source_entries),
	};
	const struct payload_mm_authvar_candidate_binding binding = {
		.generation = 1U,
		.token = 2U,
		.source_volatile_modes = PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT,
	};
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_coordinator_policy coordinator;
	struct payload_mm_authvar_coordinator_result result;
	uint8_t append_workspace[2048] __aligned(__BIGGEST_ALIGNMENT__);
	uint8_t candidate[STORE_SIZE] __aligned(__BIGGEST_ALIGNMENT__);
	uint8_t source_before[sizeof(result)];
	uint8_t auth2_before[sizeof(fixture.auth2)];
	uint8_t before[sizeof(result)];
	uint8_t store_copy[STORE_SIZE];
	uint8_t workspace_copy[sizeof(append_workspace)];
	uint8_t candidate_copy[sizeof(candidate)];
	struct payload_mm_authvar_store_entry entries_copy[64];
	struct payload_mm_authvar_store_entry source_entries_copy[64];
	struct payload_mm_authvar_coordinator_result result_copy;
	struct coordinator_fixture fixture_copy;
	bool invariant_failure = false;

	coordinator_fixture_init(&fixture);
	assert(payload_mm_authvar_store_scan(&source, media + FV_HEADER_SIZE,
		STORE_SIZE, &limits) == CB_SUCCESS);
	coordinator = (struct payload_mm_authvar_coordinator_policy) {
		.request = &fixture.policy_request,
		.owner = &coordinator_owner,
		.verify = coordinator_verify,
		.verify_context = fixture.context,
		.verify_context_size = 1U,
	};
	memset(&result, 0xa5, sizeof(result));
	memset(append_workspace, 0x3c, sizeof(append_workspace));
	memset(candidate, 0x5a, sizeof(candidate));
	memset(candidate_entries, 0x69, sizeof(candidate_entries));
	memcpy(auth2_before, fixture.auth2, sizeof(auth2_before));
	coordinator_verify_calls = 0U;
	assert(payload_mm_authvar_coordinator_prepare(&coordinator, &source,
		&policy, &binding, false, append_workspace,
		sizeof(append_workspace), candidate, sizeof(candidate),
		candidate_entries, ARRAY_SIZE(candidate_entries), &result,
		(bool *)fixture.auth2) == PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!memcmp(auth2_before, fixture.auth2, sizeof(auth2_before)) &&
		coordinator_verify_calls == 0U);
	memcpy(source_before, media + FV_HEADER_SIZE, sizeof(source_before));
	assert(payload_mm_authvar_coordinator_prepare(&coordinator, &source,
		&policy, &binding, false, append_workspace,
		sizeof(append_workspace), candidate, sizeof(candidate),
		candidate_entries, ARRAY_SIZE(candidate_entries),
		(struct payload_mm_authvar_coordinator_result *)(void *)
			(media + FV_HEADER_SIZE), &invariant_failure) ==
		PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
	assert(!memcmp(source_before, media + FV_HEADER_SIZE,
		sizeof(source_before)) && !invariant_failure &&
		coordinator_verify_calls == 0U);
	{
		const struct alias_span spans[] = {
			{ &coordinator_owner, sizeof(coordinator_owner) },
			{ (void *)source.store, source.store_size },
			{ source.entries, source.entry_count * sizeof(*source.entries) },
			{ (void *)fixture.policy_request.name,
				fixture.policy_request.name_size },
			{ (void *)fixture.policy_request.data,
				fixture.policy_request.data_size },
			{ append_workspace, sizeof(append_workspace) },
			{ candidate, sizeof(candidate) },
			{ candidate_entries, sizeof(candidate_entries) },
		};

		for (size_t i = 0U; i < ARRAY_SIZE(spans); i++) {
			uint8_t byte = *(uint8_t *)spans[i].pointer;

			memset(&result, 0xa5, sizeof(result));
			memcpy(&result_copy, &result, sizeof(result_copy));
			memcpy(store_copy, source.store, sizeof(store_copy));
			memcpy(workspace_copy, append_workspace, sizeof(workspace_copy));
			memcpy(candidate_copy, candidate, sizeof(candidate_copy));
			memcpy(entries_copy, candidate_entries, sizeof(entries_copy));
			memcpy(source_entries_copy, source_entries,
				sizeof(source_entries_copy));
			memcpy(&fixture_copy, &fixture, sizeof(fixture_copy));
			invariant_failure = false;
			assert(payload_mm_authvar_coordinator_prepare(&coordinator,
				&source, &policy, &binding, false, append_workspace,
				sizeof(append_workspace), candidate, sizeof(candidate),
				candidate_entries, ARRAY_SIZE(candidate_entries), &result,
				(bool *)spans[i].pointer) ==
				PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
			assert(*(uint8_t *)spans[i].pointer == byte &&
				!memcmp(&result, &result_copy, sizeof(result)) &&
				!memcmp(store_copy, source.store, sizeof(store_copy)) &&
				!memcmp(workspace_copy, append_workspace,
					sizeof(workspace_copy)) &&
				!memcmp(candidate_copy, candidate, sizeof(candidate_copy)) &&
				!memcmp(entries_copy, candidate_entries,
					sizeof(entries_copy)) &&
				!memcmp(source_entries_copy, source_entries,
					sizeof(source_entries_copy)) &&
				!memcmp(&fixture_copy, &fixture, sizeof(fixture_copy)) &&
				payload_mm_crypto_owner_is_clean(&coordinator_owner) &&
				coordinator_owner.allocation_count == 0U &&
				coordinator_verify_calls == 0U);
			if ((uintptr_t)spans[i].pointer % _Alignof(result))
				continue;
			memcpy(before, spans[i].pointer, sizeof(before));
			memcpy(store_copy, source.store, sizeof(store_copy));
			memcpy(workspace_copy, append_workspace, sizeof(workspace_copy));
			memcpy(candidate_copy, candidate, sizeof(candidate_copy));
			memcpy(entries_copy, candidate_entries, sizeof(entries_copy));
			memcpy(source_entries_copy, source_entries,
				sizeof(source_entries_copy));
			memcpy(&fixture_copy, &fixture, sizeof(fixture_copy));
			invariant_failure = false;
			assert(payload_mm_authvar_coordinator_prepare(&coordinator,
				&source, &policy, &binding, false, append_workspace,
				sizeof(append_workspace), candidate, sizeof(candidate),
				candidate_entries, ARRAY_SIZE(candidate_entries),
				(struct payload_mm_authvar_coordinator_result *)
					spans[i].pointer, &invariant_failure) ==
				PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER);
			assert(!memcmp(before, spans[i].pointer, sizeof(before)) &&
				!invariant_failure &&
				!memcmp(store_copy, source.store, sizeof(store_copy)) &&
				!memcmp(workspace_copy, append_workspace,
					sizeof(workspace_copy)) &&
				!memcmp(candidate_copy, candidate, sizeof(candidate_copy)) &&
				!memcmp(entries_copy, candidate_entries,
					sizeof(entries_copy)) &&
				!memcmp(source_entries_copy, source_entries,
					sizeof(source_entries_copy)) &&
				!memcmp(&fixture_copy, &fixture, sizeof(fixture_copy)) &&
				payload_mm_crypto_owner_is_clean(&coordinator_owner) &&
				coordinator_owner.allocation_count == 0U &&
				coordinator_verify_calls == 0U);
		}
	}
}

static void coordinator_recovery_case(unsigned int recovery_case,
	unsigned int fail_at)
{
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_ftw_plan plan;

	coordinator_fixture_init(&fixture);
	switch (recovery_case) {
	case 0U:
		make_write(PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		memcpy(spare(), media, BLOCK_SIZE);
		break;
	case 1U:
		make_write(PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		memcpy(spare(), media, BLOCK_SIZE);
		break;
	case 2U:
		make_write(PAYLOAD_MM_AUTHVAR_FTW_RECORD_DESTINATION_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		break;
	case 3U:
		make_workspace(spare(), PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID);
		memset(working(), 0xff, BLOCK_SIZE);
		break;
	default:
		abort();
	}
	operation_count = 0U;
	if (fail_at) {
		fail_operation = fail_at;
		coordinator_verify_calls = 0U;
		assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
			&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(fixture.result.status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_NONE);
		assert(fixture.result.volatile_modes == 0U);
		assert(fixture.result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
		assert(coordinator_verify_calls == 0U);
		fail_operation = 0U;
		/* A partial recovery marker may deliberately fail closed until reset. */
		if (poisoned)
			return;
	}
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(fixture.result.outcome == PAYLOAD_MM_AUTHVAR_OUTCOME_MUTATION &&
		coordinator_verify_calls == 1U && !poisoned);
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE, &plan) ==
		CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(erased(spare(), 2U * BLOCK_SIZE));
}

static void coordinator_recovery_count(unsigned int recovery_case)
{
	char count[32];
	int length;

	coordinator_recovery_case(recovery_case, 0U);
	assert(coordinator_verify_operation > 1U);
	length = snprintf(count, sizeof(count), "%u\n",
		coordinator_verify_operation);
	assert(length > 0 && (size_t)length < sizeof(count));
	assert(write(1, count, (unsigned long)length) == length);
}

#ifdef EXECUTOR_REAL_MEDIA
static void coordinator_reset_count(void)
{
	struct coordinator_fixture fixture;
	char count[32];
	int length;

	coordinator_fixture_build(&fixture);
	coordinator_make_user_source(&fixture, false);
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(fixture.kek_name);
	install();
	operation_count = 0U;
	assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
		&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(coordinator_verify_calls == 1U &&
		fixture.result.volatile_modes == (PAYLOAD_MM_AUTHVAR_MODE_SECURE_BOOT |
			PAYLOAD_MM_AUTHVAR_MODE_VENDOR_KEYS));
	length = snprintf(count, sizeof(count), "%u\n", operation_count);
	assert(length > 0 && (size_t)length < sizeof(count));
	assert(write(1, count, (unsigned long)length) == length);
}

static void coordinator_reset(unsigned int cut)
{
	struct coordinator_fixture fixture;
	struct payload_mm_authvar_ftw_plan plan;
	struct payload_mm_authvar_read_request read_request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		.name = coordinator_secure_name,
		.name_size = sizeof(coordinator_secure_name),
	};
	struct payload_mm_authvar_read_result read_result;
	uint8_t old_primary[BLOCK_SIZE];
	uint8_t expected_primary[BLOCK_SIZE];
	uint8_t secure_boot = 0;
	int child_status;
	pid_t child;

	coordinator_fixture_build(&fixture);
	coordinator_make_user_source(&fixture, false);
	fixture.policy_request.name = fixture.kek_name;
	fixture.policy_request.name_size = sizeof(fixture.kek_name);
	memcpy(old_primary, media, sizeof(old_primary));
	coordinator_expected_candidate(&fixture, expected_primary);
	child = fork();
	assert(child >= 0);
	if (!child) {
		operation_count = 0U;
		reset_operation = cut;
		install();
		(void)payload_mm_authvar_executor_test_coordinate(&fixture.request,
			&fixture.result);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child &&
		WIFEXITED(child_status) && WEXITSTATUS(child_status) == 77);
	operation_count = 0U;
	install();
	memcpy(read_request.vendor_guid, coordinator_global_guid,
		sizeof(read_request.vendor_guid));
	read_request.result_data = &secure_boot;
	read_request.data_capacity = sizeof(secure_boot);
	memset(&read_result, 0, sizeof(read_result));
	assert(payload_mm_authvar_read_transaction(&read_request, &read_result) ==
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(read_result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		read_result.required_data_size == sizeof(secure_boot) &&
		read_result.attributes == (PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) &&
		read_result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE &&
		secure_boot == 1U);
	assert(!memcmp(media, old_primary, sizeof(old_primary)) ||
		!memcmp(media, expected_primary, sizeof(expected_primary)));
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE, &plan) ==
		CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(erased(spare(), 2U * BLOCK_SIZE));
}

static void coordinator_private_reset(unsigned int cut, bool removing,
	bool print_count)
{
	static const uint8_t guid[16] = { 0x42 };
	static const uint8_t name[] = { 'P', 0, 'r', 0, 'i', 0, 'v', 0, 0, 0 };
	static const uint8_t certdb_guid[16] = {
		0x6e, 0xe5, 0xbe, 0xd9, 0xdc, 0x75, 0xd9, 0x49,
		0xb4, 0xd7, 0xb5, 0x34, 0x21, 0x0f, 0x63, 0x7a,
	};
	static const uint8_t certdb_name[] = {
		'c', 0, 'e', 0, 'r', 0, 't', 0, 'd', 0, 'b', 0, 0, 0,
	};
	static const uint8_t expected_binding[32] = { [0 ... 31] = 0xa5 };
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_certdb_binding stored_binding;
	struct payload_mm_authvar_ftw_plan plan;
	struct payload_mm_authvar_read_request recovery_request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		.name = coordinator_pk_name,
		.name_size = sizeof(coordinator_pk_name),
	};
	struct payload_mm_authvar_read_result recovery_result;
	struct coordinator_fixture fixture;
	const struct payload_mm_authvar_store_entry *target;
	const struct payload_mm_authvar_store_entry *certdb;
	const void *certdb_data;
	uint8_t old_media[MEDIA_SIZE];
	uint8_t expected_primary[BLOCK_SIZE];
	uint8_t recovery_data;
	bool new_state;
	int child_status;
	pid_t child;

	coordinator_private_fixture_init(&fixture);
	fixture.policy_request.name = name;
	fixture.policy_request.name_size = sizeof(name);
	memcpy(fixture.policy_request.vendor_guid, guid, sizeof(guid));
	fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
		&fixture.payload, sizeof(fixture.payload));
	coordinator_accepted_authority =
		PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	if (removing) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			install();
			assert(payload_mm_authvar_executor_test_coordinate(
				&fixture.request, &fixture.result) ==
				PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
			_exit(0);
		}
		assert(waitpid(child, &child_status, 0) == child &&
			WIFEXITED(child_status) && !WEXITSTATUS(child_status));
		coordinator_fixture_build(&fixture);
		fixture.policy_request.name = name;
		fixture.policy_request.name_size = sizeof(name);
		memcpy(fixture.policy_request.vendor_guid, guid, sizeof(guid));
		fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
			NULL, 0U);
		fixture.auth2[6] = 2U;
		coordinator_accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	}
	memcpy(old_media, media, sizeof(old_media));
	if (print_count) {
		char count[32];
		int length;

		install();
		operation_count = 0U;
		assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
			&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
		return;
	}
	child = fork();
	assert(child >= 0);
	if (!child) {
		install();
		assert(payload_mm_authvar_executor_test_coordinate(&fixture.request,
			&fixture.result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child &&
		WIFEXITED(child_status) && !WEXITSTATUS(child_status));
	memcpy(expected_primary, media, sizeof(expected_primary));
	memcpy(media, old_media, sizeof(old_media));
	coordinator_fixture_build(&fixture);
	fixture.policy_request.name = name;
	fixture.policy_request.name_size = sizeof(name);
	memcpy(fixture.policy_request.vendor_guid, guid, sizeof(guid));
	if (removing) {
		fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
			NULL, 0U);
		fixture.auth2[6] = 2U;
		coordinator_accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_PRIVATE_CERTDB;
	} else {
		fixture.policy_request.data_size = coordinator_auth2(fixture.auth2,
			&fixture.payload, sizeof(fixture.payload));
		coordinator_accepted_authority =
			PAYLOAD_MM_AUTHVAR_AUTHORITY_NEW_PRIVATE_SIGNER;
	}
	child = fork();
	assert(child >= 0);
	if (!child) {
		operation_count = 0U;
		reset_operation = cut;
		install();
		(void)payload_mm_authvar_executor_test_coordinate(&fixture.request,
			&fixture.result);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child &&
		WIFEXITED(child_status) && WEXITSTATUS(child_status) == 77);
	install();
	memcpy(recovery_request.vendor_guid, coordinator_global_guid,
		sizeof(recovery_request.vendor_guid));
	recovery_request.result_data = &recovery_data;
	recovery_request.data_capacity = sizeof(recovery_data);
	memset(&recovery_result, 0, sizeof(recovery_result));
	assert(payload_mm_authvar_read_transaction(&recovery_request,
		&recovery_result) == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert(recovery_result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		recovery_result.completion == PAYLOAD_MM_AUTHVAR_SERVICE_COMPLETE);
	new_state = !memcmp(media, expected_primary, sizeof(expected_primary));
	assert(new_state || !memcmp(media, old_media, sizeof(expected_primary)));
	target = native_find(guid, name, sizeof(name), &index, entries,
		ARRAY_SIZE(entries));
	certdb = payload_mm_authvar_store_find(&index, certdb_guid, certdb_name,
		sizeof(certdb_name));
	certdb_data = payload_mm_authvar_store_data(&index, certdb);
	assert(certdb && certdb_data);
	if (new_state != removing) {
		assert(target && target->data_size == sizeof(fixture.payload));
		assert(payload_mm_authvar_certdb_find(certdb_data, certdb->data_size,
			guid, name, sizeof(name) - 2U, &stored_binding) ==
			PAYLOAD_MM_AUTHVAR_CERTDB_OK &&
			stored_binding.size == sizeof(expected_binding) &&
			!memcmp(stored_binding.data, expected_binding,
				sizeof(expected_binding)));
	} else {
		assert(!target && payload_mm_authvar_certdb_find(certdb_data,
			certdb->data_size, guid, name, sizeof(name) - 2U,
			&stored_binding) == PAYLOAD_MM_AUTHVAR_CERTDB_NOT_FOUND);
	}
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE, &plan) ==
		CB_SUCCESS && plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(erased(spare(), 2U * BLOCK_SIZE));
}

static void coordinator_native_reset(unsigned int cut, bool print_count)
{
	static const uint8_t guid[16] = {
		0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
	};
	static const uint8_t name[] = { 'R', 0, 'e', 0, 's', 0, 0, 0 };
	static const uint8_t value[] = { 0x81, 0x42 };
	static const uint8_t zero_timestamp[16];
	struct payload_mm_authvar_store_entry entries[64];
	struct payload_mm_authvar_store_index index;
	struct payload_mm_authvar_policy_request request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_SET,
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
		.name = name,
		.name_size = sizeof(name),
		.data = value,
		.data_size = sizeof(value),
	};
	struct payload_mm_authvar_policy_result result;
	struct payload_mm_authvar_read_request read_request = {
		.operation = PAYLOAD_MM_AUTHVAR_SERVICE_GET,
		.name = name,
		.name_size = sizeof(name),
	};
	struct payload_mm_authvar_read_result read_result;
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = name,
		.name_size = sizeof(name),
		.attributes = request.attributes,
	};
	const struct payload_mm_authvar_record_span span = {
		.data = value,
		.size = sizeof(value),
	};
	struct payload_mm_authvar_ftw_plan ftw_plan;
	uint8_t old_primary[BLOCK_SIZE];
	uint8_t expected_primary[BLOCK_SIZE];
	uint8_t candidate[STORE_SIZE];
	uint8_t read_data[sizeof(value)];
	uint32_t record_size;
	uint32_t source_used;
	int child_status;
	pid_t child;
	uint64_t read_status;

	memcpy(request.vendor_guid, guid, sizeof(guid));
	memcpy(read_request.vendor_guid, guid, sizeof(guid));
	read_request.result_data = read_data;
	read_request.data_capacity = sizeof(read_data);
	memcpy(descriptor.vendor_guid, guid, sizeof(guid));
	memcpy(descriptor.timestamp, zero_timestamp, sizeof(zero_timestamp));
	make_candidate_source();
	assert(native_find(coordinator_vendor_guid, coordinator_vendor_name,
		sizeof(coordinator_vendor_name), &index, entries,
		ARRAY_SIZE(entries)));
	source_used = index.used_size;
	memcpy(old_primary, media, sizeof(old_primary));
	memset(candidate, 0xff, sizeof(candidate));
	memcpy(candidate, index.store, index.used_size);
	assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
		candidate + index.used_size, sizeof(candidate) - index.used_size,
		&record_size));
	candidate[index.used_size + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	memcpy(expected_primary, media, FV_HEADER_SIZE);
	memcpy(expected_primary + FV_HEADER_SIZE, candidate, sizeof(candidate));
	if (print_count) {
		char count[32];
		int length;

		coordinator_install_executor();
		operation_count = 0U;
		assert(payload_mm_authvar_policy_transaction(&request, &result) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(!memcmp(media, expected_primary, sizeof(expected_primary)));
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
		return;
	}
	child = fork();
	assert(child >= 0);
	if (!child) {
		coordinator_install_executor();
		operation_count = 0U;
		reset_operation = cut;
		(void)payload_mm_authvar_policy_transaction(&request, &result);
		_exit(0);
	}
	assert(waitpid(child, &child_status, 0) == child &&
		WIFEXITED(child_status) && WEXITSTATUS(child_status) == 77);
	coordinator_install_executor();
	memset(&read_result, 0, sizeof(read_result));
	read_status = payload_mm_authvar_read_transaction(&read_request,
		&read_result);
	assert(read_status == read_result.status &&
		(read_status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ||
		 read_status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND));
	if (!memcmp(media, expected_primary, sizeof(expected_primary)))
		assert(read_result.status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
			!memcmp(read_data, value, sizeof(value)));
	else {
		const size_t first = FV_HEADER_SIZE + source_used;
		const size_t state = first + 2U;
		const size_t last = first + record_size;

		assert(read_result.status == PAYLOAD_MM_AUTHVAR_STATUS_NOT_FOUND);
		assert(!memcmp(media, old_primary, first));
		assert(!memcmp(media + last, old_primary + last,
			sizeof(old_primary) - last));
		for (size_t i = first; i < last; i++) {
			assert((media[i] | old_primary[i]) == old_primary[i]);
			if (i == state)
				assert(media[i] == PAYLOAD_MM_AUTHVAR_STATE_ERASED ||
					media[i] ==
						PAYLOAD_MM_AUTHVAR_STATE_HEADER_VALID_ONLY);
			else
				assert((media[i] & expected_primary[i]) ==
					expected_primary[i]);
		}
		assert(!native_find(guid, name, sizeof(name), &index, entries,
			ARRAY_SIZE(entries)) && index.entry_count == 1U &&
			coordinator_vendor_value() == candidate_source_vendor_value);
	}
	assert(payload_mm_authvar_ftw_plan(media, MEDIA_SIZE, BLOCK_SIZE,
		&ftw_plan) == CB_SUCCESS &&
		ftw_plan.action == PAYLOAD_MM_AUTHVAR_FTW_CLEAN);
	assert(erased(spare(), 2U * BLOCK_SIZE));
}
#endif
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
void payload_mm_authvar_recovery_test_arm_image_mutation(void *image,
	size_t size, uint32_t read_offset, size_t read_size,
	unsigned int matches_to_skip)
{
	assert(image && size && !recovery_image_to_mutate &&
		!recovery_image_to_mutate_size);
	recovery_image_to_mutate = image;
	recovery_image_to_mutate_size = size;
	recovery_image_mutation_read_offset = read_offset;
	recovery_image_mutation_read_size = read_size;
	recovery_image_mutation_matches_to_skip = matches_to_skip;
}

static void recovery_planner_test(const char *name)
{
	enum payload_mm_authvar_recovery_test_mutation mutation =
		PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_NONE;
	bool execute = strcmp(name, "planner-zero-write") != 0;
	uint64_t expected = PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;

	memset(working(), 0xff, BLOCK_SIZE);
	if (!strcmp(name, "planner-abort-read-image-mutation")) {
		make_write(PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_ALLOCATED);
		mutation = PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_ABORT_READ_IMAGE;
	} else if (!strcmp(name, "planner-replay-read-image-mutation")) {
		make_write(PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		memcpy(spare(), media, BLOCK_SIZE);
		mutation = PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_REPLAY_READ_IMAGE;
	}
	if (!strcmp(name, "planner-snapshot-mutation"))
		mutation = PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_SNAPSHOT;
	else if (!strcmp(name, "planner-geometry-mutation"))
		mutation = PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_GEOMETRY;
	else if (!strcmp(name, "planner-step-mutation"))
		mutation = PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_STEP;
	else if (!strcmp(name, "planner-token-mutation"))
		mutation = PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_TOKEN;
	else if (!strcmp(name, "planner-callback-image-mutation"))
		mutation = PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_CALLBACK_IMAGE;
	else if (!strcmp(name, "planner-abort-read-image-mutation") ||
		 !strcmp(name, "planner-replay-read-image-mutation"))
		;
	else if (!strcmp(name, "planner-zero-write") ||
		 !strcmp(name, "planner-execute"))
		expected = PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS;
	else
		assert(false);
	install();
	assert(payload_mm_authvar_executor_test_recovery_plan(mutation, execute) ==
		expected);
	if (!execute || mutation != PAYLOAD_MM_AUTHVAR_RECOVERY_TEST_NONE)
		assert(program_count == 0U && erase_count == 0U);
	else
		assert(program_count && erase_count);
}
#endif

#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
static bool mor_protected(void *unused, const void *storage, size_t size)
{
	(void)unused;
	return storage && size;
}

static struct payload_mm_authvar_mor_grant mor_grant(uint8_t value)
{
	struct payload_mm_authvar_mor_grant grant = {
		.revision = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REVISION,
		.size = sizeof(grant),
		.cold_boot_generation = 1U,
		.entry = { .present = 1U, .value = value },
		.flags = PAYLOAD_MM_AUTHVAR_MOR_GRANT_REQUIRED_FLAGS,
		.dma_policy_generation = 2U,
		.inventory_generation = 3U,
		.total_bytes = 0x1000U,
		.cleared_bytes = 0x1000U,
		.total_spans = 1U,
		.cleared_spans = 1U,
		.spans = { {
			.base = 0x1000U,
			.size = 0x1000U,
			.span_class = PAYLOAD_MM_AUTHVAR_MOR_GRANT_SPAN_CLEARED,
		} },
	};

	for (size_t i = 0; i < sizeof(grant.dma_policy_identity); i++) {
		grant.dma_policy_identity[i] = (uint8_t)i + 1U;
		grant.inventory_identity[i] = (uint8_t)i + 0x41U;
	}
	return grant;
}

static uint32_t mor_append_control(uint8_t value)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = identity->name,
		.name_size = identity->name_size,
		.attributes = PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES,
	};
	const struct payload_mm_authvar_record_span span = {
		.data = &value,
		.size = 1U,
	};
	uint32_t record_size;

	memcpy(descriptor.vendor_guid, identity->vendor_guid,
		sizeof(descriptor.vendor_guid));
	assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
		media + FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE,
		STORE_SIZE - PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE, &record_size));
	media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 2U] =
		PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	return PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + record_size;
}

static uint32_t mor_append_filler(uint32_t used)
{
	static const uint8_t guid[16] = { 0x71U };
	static const uint8_t name[] = { 'F', 0, 0, 0 };
	static const uint8_t data = 0x5aU;
	struct payload_mm_authvar_record_descriptor descriptor = {
		.name = name,
		.name_size = sizeof(name),
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
	};
	const struct payload_mm_authvar_record_span span = {
		.data = &data,
		.size = sizeof(data),
	};
	uint32_t record_size;

	assert(used < STORE_SIZE);
	memcpy(descriptor.vendor_guid, guid, sizeof(guid));
	assert(payload_mm_authvar_record_encode(&descriptor, &span, 1U,
		PAYLOAD_MM_AUTHVAR_RECORD_TIMESTAMP_VALIDATED,
		media + FV_HEADER_SIZE + used, STORE_SIZE - used, &record_size));
	media[FV_HEADER_SIZE + used + 2U] = PAYLOAD_MM_AUTHVAR_STATE_ADDED;
	return used + record_size;
}

static uint8_t mor_read_control(void)
{
	const struct payload_mm_authvar_mor_identity *identity =
		&payload_mm_authvar_mor_control_identity;
	const struct payload_mm_authvar_store_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128U,
		.maximum_data_size = 2048U,
		.maximum_records = 64U,
	};
	struct payload_mm_authvar_store_entry entry;
	bool found;

	assert(payload_mm_authvar_store_find_one(&entry, &found,
		media + FV_HEADER_SIZE, STORE_SIZE, &limits,
		identity->vendor_guid, identity->name, identity->name_size) ==
		CB_SUCCESS);
	assert(found && entry.data_size == 1U);
	return media[FV_HEADER_SIZE + entry.data_offset];
}

static void mor_control_clear_test(const char *name)
{
	uint8_t value = !strcmp(name, "mor-upper-bits") ? 0xa5U : 1U;
	struct payload_mm_authvar_mor_grant grant;
	uint32_t used = PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE;
	uint64_t status;
	bool fault_case = !strncmp(name, "mor-fault-", 10U);
	bool mutate_record = strstr(name, "mutate-record") != NULL;
	bool mutate_copies = strstr(name, "mutate-copies") != NULL;
	bool mutate_canonical = strstr(name, "mutate-canonical") != NULL;
	bool mutate_grant = strstr(name, "-grant") != NULL;
	bool mutation_case = mutate_record || mutate_copies || mutate_canonical;

	if (fault_case) {
		const char *digit = name + 10U;

		fail_operation = 0U;
		while (*digit >= '0' && *digit <= '9') {
			fail_operation = fail_operation * 10U +
				(unsigned int)(*digit - '0');
			digit++;
		}
		assert(fail_operation && !*digit);
	}

	if (strcmp(name, "mor-missing"))
		used = mor_append_control(value);
	if (mutate_copies)
		used = mor_append_filler(used);
	if (!strcmp(name, "mor-recovery-abort")) {
		make_write(PAYLOAD_MM_AUTHVAR_FTW_STATE_ERASED,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		memcpy(spare(), media, BLOCK_SIZE);
	} else if (!strcmp(name, "mor-recovery-replay")) {
		make_write(PAYLOAD_MM_AUTHVAR_FTW_RECORD_SPARE_COMPLETE,
			PAYLOAD_MM_AUTHVAR_FTW_HEADER_WRITES_ALLOCATED);
		memcpy(spare(), media, BLOCK_SIZE);
	} else if (!strcmp(name, "mor-reclaim") || mutate_copies) {
		assert(used < STORE_SIZE);
		media[FV_HEADER_SIZE + used] = 0xaaU;
	} else if (!strcmp(name, "mor-bad-attributes")) {
		put32(media + FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE + 4U,
			PAYLOAD_MM_AUTHVAR_MOR_ATTRIBUTES &
				~PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS);
	}
	install();
	if (mutation_case) {
		mor_seal_mutation = mutate_record ?
			PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_RECORD_PAIR :
			mutate_copies ? PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_COPIES_PAIR :
			PAYLOAD_MM_AUTHVAR_MOR_SEAL_TEST_CANONICAL_PAIR;
		mor_seal_mutate_on_grant = mutate_grant;
		mor_seal_mutate_on_read = !mutate_grant;
	}
	grant = mor_grant(!strcmp(name, "mor-grant-mismatch") ? 3U : value);
	if (strncmp(name, "mor-no-grant", 12U))
		assert(payload_mm_authvar_mor_grant_install(&grant, mor_protected, NULL) ==
			CB_SUCCESS);
	status = payload_mm_authvar_mor_control_clear_transaction();
	if (mutation_case) {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(!mor_seal_mutate_on_grant && !mor_seal_mutate_on_read);
		assert(program_count == 0U && erase_count == 0U && end_count == 1U);
		return;
	}
	if (fault_case) {
		if (status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS)
			assert(fail_operation > operation_count);
		else
			assert(fail_operation <= operation_count);
		if (fail_operation <= 5U)
			assert(program_count == 0U && erase_count == 0U);
		return;
	}
	if (!strcmp(name, "mor-success") || !strcmp(name, "mor-upper-bits") ||
	    !strcmp(name, "mor-recovery-abort") ||
	    !strcmp(name, "mor-recovery-replay") ||
	    !strcmp(name, "mor-reclaim")) {
		if (status != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS) {
			char message[160];
			int length = snprintf(message, sizeof(message),
				"MOR clear status: 0x%llx, programs=%u, erases=%u, reads=%u, poisoned=%u\n",
				(unsigned long long)status, program_count, erase_count,
				read_count, poisoned);

			if (length > 0)
				(void)write(2, message, (unsigned long)length);
		}
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(mor_read_control() == (uint8_t)(value & ~1U));
		assert(program_count && end_count == 1U && !poisoned);
		assert(payload_mm_authvar_mor_control_clear_transaction() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	} else {
		assert(status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(program_count == 0U && erase_count == 0U && end_count == 1U);
	}
}
#endif

int main(int argc, char **argv)
{
	assert(argc == 2);
#ifdef EXECUTOR_REAL_MEDIA
	real_media_prepare();
#endif
	make_clean();
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
#ifdef EXECUTOR_REAL_MEDIA
	if (!strcmp(argv[1], "count-fmp-initialize")) {
		struct payload_mm_fmp_owner_record output;
		char count[32];
		int length;
		bool seen[TEST_CALLBACK_END + 1U] = { false };

		fmp_prepare_modes();
		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0x12345678U);
		install();
		assert(payload_mm_authvar_fmp_state_initialize(&output) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		for (size_t i = 0; i < callback_trace_count; i++)
			seen[callback_trace[i]] = true;
		assert(seen[TEST_CALLBACK_BEGIN] && seen[TEST_CALLBACK_READ] &&
			seen[TEST_CALLBACK_PROGRAM] && seen[TEST_CALLBACK_SYNC] &&
			seen[TEST_CALLBACK_END]);
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
		return 0;
	} else if (!strcmp(argv[1], "count-fmp-initialize-reclaim")) {
		struct payload_mm_fmp_owner_record output;
		char count[32];
		int length;
		bool seen[TEST_CALLBACK_END + 1U] = { false };

		fmp_prepare_modes();
		fmp_append_legacy(PAYLOAD_MM_FMP_STATE_KEY_VERSION, 0x12345678U);
		fmp_dirty_tail();
		install();
		assert(payload_mm_authvar_fmp_state_initialize(&output) ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		for (size_t i = 0; i < callback_trace_count; i++)
			seen[callback_trace[i]] = true;
		assert(seen[TEST_CALLBACK_BEGIN] && seen[TEST_CALLBACK_READ] &&
			seen[TEST_CALLBACK_PROGRAM] && seen[TEST_CALLBACK_ERASE] &&
			seen[TEST_CALLBACK_SYNC] && seen[TEST_CALLBACK_END]);
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
		return 0;
	} else if (!strncmp(argv[1], "reset-fmp-initialize-reclaim-", 29U)) {
		unsigned int cut = parse_cut(argv[1] + 29U);

		assert(cut);
		fmp_reset_initialize(cut, true);
		return 0;
	} else if (!strncmp(argv[1], "reset-fmp-initialize-", 21U)) {
		unsigned int cut = parse_cut(argv[1] + 21U);

		assert(cut);
		fmp_reset_initialize(cut, false);
		return 0;
	}
#endif
	if (!strncmp(argv[1], "fmp-", 4U)) {
		if (!strcmp(argv[1], "fmp-runtime-read") ||
		    !strcmp(argv[1], "fmp-runtime-compare") ||
		    !strcmp(argv[1], "fmp-busy-reentry") ||
		    !strncmp(argv[1], "fmp-callback-mutate-", 20U) ||
		    !strcmp(argv[1], "fmp-admission-invalid-operation") ||
		    !strcmp(argv[1], "fmp-admission-read-current") ||
		    !strcmp(argv[1], "fmp-admission-read-candidate") ||
		    !strncmp(argv[1], "fmp-admission-null-", 19U) ||
		    !strncmp(argv[1], "fmp-admission-misaligned-", 25U) ||
		    !strncmp(argv[1], "fmp-admission-unprotected-", 26U) ||
		    !strncmp(argv[1], "fmp-admission-alias-", 20U) ||
		    !strncmp(argv[1], "fmp-admission-arena-", 20U) ||
		    !strncmp(argv[1], "fmp-admission-media-", 20U) ||
		    !strncmp(argv[1], "fmp-admission-state-owner-", 26U) ||
		    !strncmp(argv[1], "fmp-admission-state-authority-", 30U) ||
		    !strncmp(argv[1], "fmp-admission-authvar-owner-", 28U) ||
		    !strncmp(argv[1], "fmp-admission-owner-", 20U)) {
			fmp_hostile_case(argv[1]);
			return 0;
		}
		fmp_state_case(argv[1]);
		return 0;
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_RECOVERY_PLANNER)
	if (!strncmp(argv[1], "planner-", 8U)) {
		recovery_planner_test(argv[1]);
		return 0;
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_MOR_CONTROL_CLEAR_TRANSACTION)
	if (!strncmp(argv[1], "mor-", 4U)) {
		mor_control_clear_test(argv[1]);
		return 0;
	}
#endif
#if CONFIG(PAYLOAD_MM_AUTHVAR_COORDINATOR)
	if (!strcmp(argv[1], "coordinator-success")) {
		coordinator_success();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-private-atomic")) {
		coordinator_private_atomic(false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-private-provider")) {
		coordinator_private_atomic(true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-preflight-consumers")) {
		coordinator_preflight_consumers();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-ordinary")
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
		   || !strcmp(argv[1], "coordinator-native-ordinary-sha-armed")
#endif
		   ) {
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
		if (strstr(argv[1], "sha-armed"))
			fmp_sha_fail = true;
#endif
		coordinator_native_ordinary();
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
		assert(!fmp_record_sha_calls &&
			(!strstr(argv[1], "sha-armed") || fmp_sha_fail));
#endif
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-collision")) {
		coordinator_native_collision_precedence();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-invalid")) {
		coordinator_native_invalid();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-post-collision")) {
		coordinator_native_post_collision();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-reclaim")) {
		coordinator_native_reclaim();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-copy")) {
		coordinator_native_copy_isolation();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-invalid")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_INVALID,
			PAYLOAD_MM_AUTHVAR_STATUS_INVALID_PARAMETER, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-busy")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_BUSY,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-no-memory")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_NO_MEMORY,
			PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-malformed")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_MALFORMED,
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-rejected")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_REJECTED,
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-unsupported")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_UNSUPPORTED,
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-internal")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_INTERNAL,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-changed")) {
		coordinator_status_case(PAYLOAD_MM_VERIFY_CHANGED,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-status-out-of-range")) {
		coordinator_status_case((enum payload_mm_verify_status)99,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-noop")) {
		coordinator_no_write_case(true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-not-found")) {
		coordinator_no_write_case(false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-owner")) {
		coordinator_attack_case(COORDINATOR_ATTACK_OWNER_ABANDON, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-alternate")) {
		coordinator_attack_case(
			COORDINATOR_ATTACK_ALTERNATE_OWNER_ABANDON, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-owner-dirty")) {
		coordinator_attack_case(COORDINATOR_ATTACK_OWNER_DIRTY, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-descriptor")) {
		coordinator_attack_case(COORDINATOR_ATTACK_DESCRIPTOR, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-request")) {
		coordinator_attack_case(COORDINATOR_ATTACK_REQUEST, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-name")) {
		coordinator_attack_case(COORDINATOR_ATTACK_NAME, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-data")) {
		coordinator_attack_case(COORDINATOR_ATTACK_DATA, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-context")) {
		coordinator_attack_case(COORDINATOR_ATTACK_CONTEXT, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-internal-context")) {
		coordinator_attack_case(COORDINATOR_ATTACK_INTERNAL_CONTEXT, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-attack-result")) {
		coordinator_attack_case(COORDINATOR_ATTACK_RESULT, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-context-max")) {
		coordinator_context_boundary(true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-context-over")) {
		coordinator_context_boundary(false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-runtime-pk-delete")) {
		coordinator_runtime_pk_delete();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-runtime-missing-enable")) {
		coordinator_runtime_missing_enable();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-vendor-reconcile")) {
		coordinator_vendor_reconcile();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-nonboolean-modes")) {
		coordinator_nonboolean_modes();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-reconcile")) {
		coordinator_native_reconcile();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-end-failure")) {
		coordinator_end_failure_recovery();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-runtime-end-failure")) {
		coordinator_runtime_end_failure();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-preexisting-crypto")) {
		coordinator_preexisting_crypto_owner();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-admission")) {
		coordinator_admission_matrix();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-result-read")) {
		coordinator_media_attack_case(COORDINATOR_MEDIA_ATTACK_RESULT,
			TEST_CALLBACK_READ);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-result-program")) {
		coordinator_media_attack_case(COORDINATOR_MEDIA_ATTACK_RESULT,
			TEST_CALLBACK_PROGRAM);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-result-erase")) {
		coordinator_media_attack_case(COORDINATOR_MEDIA_ATTACK_RESULT,
			TEST_CALLBACK_ERASE);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-result-end")) {
		coordinator_media_attack_case(COORDINATOR_MEDIA_ATTACK_RESULT,
			TEST_CALLBACK_END);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-owner-program")) {
		coordinator_media_attack_case(COORDINATOR_MEDIA_ATTACK_OWNER_DIRTY,
			TEST_CALLBACK_PROGRAM);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-owner-read")) {
		coordinator_media_attack_case(COORDINATOR_MEDIA_ATTACK_OWNER_DIRTY,
			TEST_CALLBACK_READ);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-alternate-read")) {
		coordinator_media_attack_case(
			COORDINATOR_MEDIA_ATTACK_ALTERNATE_OWNER,
			TEST_CALLBACK_READ);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-alternate-erase")) {
		coordinator_media_attack_case(
			COORDINATOR_MEDIA_ATTACK_ALTERNATE_OWNER,
			TEST_CALLBACK_ERASE);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-media-context-read")) {
		coordinator_media_attack_case(COORDINATOR_MEDIA_ATTACK_CONTEXT,
			TEST_CALLBACK_READ);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-callback-trace")) {
		coordinator_callback_trace();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-callback-count")) {
		char count[32];
		int length;

		coordinator_callback_trace();
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-begin-result")) {
		coordinator_begin_attack_case(COORDINATOR_MEDIA_ATTACK_RESULT);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-begin-context")) {
		coordinator_begin_attack_case(COORDINATOR_MEDIA_ATTACK_CONTEXT);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-begin-owner")) {
		coordinator_begin_attack_case(COORDINATOR_MEDIA_ATTACK_OWNER_DIRTY);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-begin-alternate")) {
		coordinator_begin_attack_case(
			COORDINATOR_MEDIA_ATTACK_ALTERNATE_OWNER);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-callback-device-", 28U)) {
		coordinator_callback_fault(candidate_fault_number(argv[1] + 28U),
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-callback-unsupported-", 33U)) {
		coordinator_callback_fault(candidate_fault_number(argv[1] + 33U),
			PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-callback-write-protected-", 37U)) {
		coordinator_callback_fault(candidate_fault_number(argv[1] + 37U),
			PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-callback-invalid-", 29U)) {
		coordinator_callback_fault(candidate_fault_number(argv[1] + 29U),
			(enum payload_mm_authvar_media_result)99);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-bundle-invalid")) {
		coordinator_forced_fault(PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_INVALID,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-bundle-internal")) {
		coordinator_forced_fault(PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_INTERNAL,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-bundle-changed")) {
		coordinator_forced_fault(PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_CHANGED,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-bundle-unsupported")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_UNSUPPORTED,
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-bundle-rejected")) {
		coordinator_forced_fault(PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_REJECTED,
			PAYLOAD_MM_AUTHVAR_STATUS_SECURITY_VIOLATION, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-outcome-none")) {
		coordinator_forced_fault(PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_OUTCOME_NONE,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-bundle-outcome-none")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_OUTCOME_NONE,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-bundle-outcome-range")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_BUNDLE_OUTCOME_RANGE,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-noop-notfound")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NOOP_NOT_FOUND,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-notfound-success")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_NOT_FOUND_SUCCESS,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-mutation-notfound")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_MUTATION_NOT_FOUND,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-candidate-invalid")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_INVALID,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-candidate-security")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_SECURITY,
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR, true);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-force-candidate-no-memory")) {
		coordinator_forced_fault(
			PAYLOAD_MM_AUTHVAR_COORDINATOR_FAULT_CANDIDATE_NO_MEMORY,
			PAYLOAD_MM_AUTHVAR_STATUS_OUT_OF_RESOURCES, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-abort")) {
		coordinator_recovery_case(0U, 0U);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-replay")) {
		coordinator_recovery_case(1U, 0U);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-complete")) {
		coordinator_recovery_case(2U, 0U);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-restore")) {
		coordinator_recovery_case(3U, 0U);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-count-abort")) {
		coordinator_recovery_count(0U);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-count-replay")) {
		coordinator_recovery_count(1U);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-count-complete")) {
		coordinator_recovery_count(2U);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-recovery-count-restore")) {
		coordinator_recovery_count(3U);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-recovery-fault-abort-", 33U)) {
		coordinator_recovery_case(0U,
			candidate_fault_number(argv[1] + 33U));
		return 0;
	} else if (!strncmp(argv[1], "coordinator-recovery-fault-replay-", 34U)) {
		coordinator_recovery_case(1U,
			candidate_fault_number(argv[1] + 34U));
		return 0;
	} else if (!strncmp(argv[1], "coordinator-recovery-fault-complete-", 36U)) {
		coordinator_recovery_case(2U,
			candidate_fault_number(argv[1] + 36U));
		return 0;
	} else if (!strncmp(argv[1], "coordinator-recovery-fault-restore-", 35U)) {
		coordinator_recovery_case(3U,
			candidate_fault_number(argv[1] + 35U));
		return 0;
	} else if (!strcmp(argv[1], "coordinator-policy-race")) {
		coordinator_policy_race();
		return 0;
	} else if (!strcmp(argv[1], "coordinator-output-alias")) {
		coordinator_output_alias();
		return 0;
#ifdef EXECUTOR_REAL_MEDIA
	} else if (!strcmp(argv[1], "coordinator-reset-count")) {
		coordinator_reset_count();
		return 0;
	} else if (!strncmp(argv[1], "coordinator-reset-", 18U)) {
		coordinator_reset(candidate_fault_number(argv[1] + 18U));
		return 0;
	} else if (!strcmp(argv[1], "coordinator-private-add-reset-count")) {
		coordinator_private_reset(0U, false, true);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-private-add-reset-", 30U)) {
		coordinator_private_reset(candidate_fault_number(argv[1] + 30U),
			false, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-private-remove-reset-count")) {
		coordinator_private_reset(0U, true, true);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-private-remove-reset-", 33U)) {
		coordinator_private_reset(candidate_fault_number(argv[1] + 33U),
			true, false);
		return 0;
	} else if (!strcmp(argv[1], "coordinator-native-reset-count")) {
		coordinator_native_reset(0U, true);
		return 0;
	} else if (!strncmp(argv[1], "coordinator-native-reset-", 25U)) {
		coordinator_native_reset(candidate_fault_number(argv[1] + 25U), false);
		return 0;
#endif
	}
#endif
	#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE_COMMIT)
	if (!strcmp(argv[1], "candidate-success")) {
		candidate_commit_case(CANDIDATE_PREPARE_OK, 0U, true);
		return 0;
	} else if (!strcmp(argv[1], "candidate-uninstalled")) {
		candidate_admission_case(false);
		return 0;
	} else if (!strcmp(argv[1], "candidate-corrupt-policy")) {
		candidate_admission_case(true);
		return 0;
	} else if (!strcmp(argv[1], "candidate-binding")) {
		candidate_commit_case(CANDIDATE_PREPARE_BINDING, 0U, false);
		assert(program_count == 0U && erase_count == 0U);
		return 0;
	} else if (!strcmp(argv[1], "candidate-header")) {
		candidate_commit_case(CANDIDATE_PREPARE_HEADER, 0U, false);
		assert(program_count == 0U && erase_count == 0U);
		return 0;
	} else if (!strcmp(argv[1], "candidate-source")) {
		candidate_commit_case(CANDIDATE_PREPARE_SOURCE, 0U, false);
		assert(program_count == 0U && erase_count == 0U);
		return 0;
	} else if (!strcmp(argv[1], "candidate-index")) {
		candidate_commit_case(CANDIDATE_PREPARE_INDEX, 0U, false);
		assert(program_count == 0U && erase_count == 0U);
		return 0;
	} else if (!strcmp(argv[1], "candidate-output")) {
		candidate_commit_case(CANDIDATE_PREPARE_OUTPUT, 0U, true);
		return 0;
	} else if (!strncmp(argv[1], "candidate-reject-", 17U)) {
		enum candidate_prepare_mode mode;

		assert(candidate_reject_mode(argv[1] + 17U, &mode));
		candidate_commit_case(mode, 0U, false);
		assert(program_count == 0U && erase_count == 0U);
		return 0;
	} else if (!strcmp(argv[1], "candidate-count")) {
		char count[32];
		int length;

		candidate_commit_case(CANDIDATE_PREPARE_OK, 0U, true);
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
		return 0;
	} else if (!strncmp(argv[1], "candidate-fault-", 16U)) {
		unsigned int fault_at = candidate_fault_number(argv[1] + 16U);

		assert(fault_at);
		candidate_commit_case(CANDIDATE_PREPARE_OK, fault_at, false);
		return 0;
	} else if (!strncmp(argv[1], "candidate-result-bit-", 21U)) {
		unsigned int offset;
		unsigned int bit;

		candidate_fault_pair(argv[1] + 21U, &offset, &bit);
		candidate_result_bit_case(offset, bit);
		return 0;
	} else if (!strncmp(argv[1], "candidate-image-byte-", 21U)) {
		unsigned int offset = candidate_fault_number(argv[1] + 21U);

		assert(offset < BLOCK_SIZE);
		candidate_image_mutation_offset = offset;
		candidate_image_mutation_bit = offset & 7U;
		candidate_commit_case(CANDIDATE_PREPARE_IMAGE_CALLBACK, 0U, false);
		assert(program_count && !mutate_candidate_header_on_program);
		assert(!ftw_spare_marker_programmed &&
			!ftw_destination_marker_programmed &&
			primary_erase_count == 0U);
		return 0;
	} else if (!strncmp(argv[1], "candidate-callback-device-", 26U)) {
		candidate_callback_fault_case(candidate_fault_number(argv[1] + 26U),
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
		return 0;
	} else if (!strncmp(argv[1], "candidate-callback-unsupported-", 31U)) {
		candidate_callback_fault_case(candidate_fault_number(argv[1] + 31U),
			PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED);
		return 0;
	} else if (!strncmp(argv[1], "candidate-callback-write-protected-", 35U)) {
		candidate_callback_fault_case(candidate_fault_number(argv[1] + 35U),
			PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED);
		return 0;
	} else if (!strcmp(argv[1], "candidate-image-callback")) {
		candidate_commit_case(CANDIDATE_PREPARE_IMAGE_CALLBACK, 0U, false);
		assert(program_count && !mutate_candidate_header_on_program);
		assert(!ftw_spare_marker_programmed &&
			!ftw_destination_marker_programmed &&
			primary_erase_count == 0U);
		return 0;
	} else if (!strcmp(argv[1], "candidate-source-callback")) {
		candidate_commit_case(CANDIDATE_PREPARE_SOURCE_CALLBACK, 0U, false);
		assert(program_count && !mutate_candidate_source_on_program);
		assert(primary_erase_count == 0U);
		return 0;
	} else if (!strcmp(argv[1], "candidate-dirty-tail")) {
		candidate_commit_case(CANDIDATE_PREPARE_DIRTY_TAIL, 0U, false);
		return 0;
	} else if (!strcmp(argv[1], "candidate-unsupported-read")) {
		candidate_media_error_case(CANDIDATE_MEDIA_FAULT_READ,
			PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED,
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		return 0;
	} else if (!strcmp(argv[1], "candidate-unsupported-program")) {
		candidate_media_error_case(CANDIDATE_MEDIA_FAULT_PROGRAM,
			PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED,
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		return 0;
	} else if (!strcmp(argv[1], "candidate-unsupported-erase")) {
		candidate_media_error_case(CANDIDATE_MEDIA_FAULT_ERASE,
			PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED,
			PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED);
		return 0;
	} else if (!strcmp(argv[1], "candidate-write-protected-read")) {
		candidate_media_error_case(CANDIDATE_MEDIA_FAULT_READ,
			PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED,
			PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
		return 0;
	} else if (!strcmp(argv[1], "candidate-write-protected-program")) {
		candidate_media_error_case(CANDIDATE_MEDIA_FAULT_PROGRAM,
			PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED,
			PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
		return 0;
	} else if (!strcmp(argv[1], "candidate-write-protected-erase")) {
		candidate_media_error_case(CANDIDATE_MEDIA_FAULT_ERASE,
			PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED,
			PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED);
		return 0;
#ifdef EXECUTOR_REAL_MEDIA
	} else if (!strncmp(argv[1], "reset-candidate-", 16U)) {
		const char *cuts = argv[1] + 16U;
		const char *separator = strchr(cuts, '-');
		unsigned int cut;
		unsigned int recovery_cut = 0;
		char first[16];

		if (separator) {
			size_t length = (size_t)(separator - cuts);

			assert(length && length < sizeof(first));
			memcpy(first, cuts, length);
			first[length] = 0;
			cut = candidate_fault_number(first);
			recovery_cut = candidate_fault_number(separator + 1U);
		} else {
			cut = candidate_fault_number(cuts);
		}
		assert(cut);
		reset_candidate_commit(cut, recovery_cut, false);
		return 0;
	} else if (!strncmp(argv[1], "candidate-recovery-count-", 25U)) {
		unsigned int cut = candidate_fault_number(argv[1] + 25U);

		assert(cut);
		reset_candidate_commit(cut, 0U, true);
		return 0;
#endif
	}
#endif
	if (!strcmp(argv[1], "misaligned-arena")) {
		reject_misaligned_arena();
	} else if (!strcmp(argv[1], "second-install")) {
		reject_second_install();
	} else if (!strcmp(argv[1], "invalid-geometry")) {
		reject_invalid_geometry();
	} else if (!strcmp(argv[1], "null-arena") ||
		   !strcmp(argv[1], "zero-arena") ||
		   !strcmp(argv[1], "null-limits") ||
		   !strcmp(argv[1], "undersize-arena") ||
		   !strcmp(argv[1], "record-limits") ||
		   !strcmp(argv[1], "geometry-limits") ||
		   !strcmp(argv[1], "overflow-layout")) {
		reject_install(argv[1]);
	} else if (!strcmp(argv[1], "direct-add")) {
		direct_write(WRITE_ADD);
#if CONFIG(PAYLOAD_MM_FMP_OWNER_AUTHVAR)
	} else if (!strcmp(argv[1], "generic-get") ||
		   !strcmp(argv[1], "generic-get-sha-armed")) {
		if (strstr(argv[1], "sha-armed"))
			fmp_sha_fail = true;
		generic_get();
		assert(!strstr(argv[1], "sha-armed") || fmp_sha_fail);
	} else if (!strcmp(argv[1], "generic-authvar-owner-output-full") ||
		   !strcmp(argv[1], "generic-authvar-owner-output-partial")) {
		generic_authvar_owner_output(strstr(argv[1], "partial") != NULL);
#endif
	} else if (!strcmp(argv[1], "marker-expected-state")) {
		direct_write(WRITE_MARKER_EXPECTED_MUTATE);
	} else if (!strcmp(argv[1], "marker-nor-clear")) {
#ifdef EXECUTOR_SOURCE_INCLUDE
		reject_illegal_nor_marker();
#else
		assert(false);
#endif
	} else if (!strcmp(argv[1], "direct-replace")) {
		direct_write(WRITE_REPLACE);
	} else if (!strcmp(argv[1], "reclaim")) {
		direct_write(WRITE_RECLAIM);
	} else if (!strcmp(argv[1], "reclaim-precommit-scan")) {
		direct_write(WRITE_RECLAIM_SCAN_REJECT);
	} else if (!strcmp(argv[1], "spare-suffix-mutation")) {
		direct_write(WRITE_SPARE_SUFFIX_MUTATE);
	} else if (!strcmp(argv[1], "spare-body-mutation")) {
		direct_write(WRITE_SPARE_BODY_MUTATE);
	} else if (!strcmp(argv[1], "queue-header-mutation")) {
		direct_write(WRITE_QUEUE_HEADER_MUTATE);
	} else if (!strcmp(argv[1], "queue-record-mutation")) {
		direct_write(WRITE_QUEUE_RECORD_MUTATE);
	} else if (!strcmp(argv[1], "primary-body-mutation")) {
		direct_write(WRITE_PRIMARY_BODY_MUTATE);
	} else if (!strcmp(argv[1], "direct-delete")) {
		direct_write(WRITE_DELETE);
	} else if (!strcmp(argv[1], "empty-append-existing")) {
		direct_write(WRITE_EMPTY_APPEND_EXISTING);
	} else if (!strcmp(argv[1], "empty-append-absent")) {
		direct_write(WRITE_EMPTY_APPEND_ABSENT);
	} else if (!strcmp(argv[1], "seal-mutation")) {
		direct_write(WRITE_MUTATE);
	} else if (!strcmp(argv[1], "record-state-mutation")) {
		direct_write(WRITE_RECORD_STATE_MUTATE);
	} else if (!strcmp(argv[1], "body-readback-mutation")) {
		direct_write(WRITE_BODY_READBACK_MUTATE);
	} else if (!strcmp(argv[1], "final-compare-mutation")) {
		direct_write(WRITE_FINAL_COMPARE_MUTATE);
	} else if (!strcmp(argv[1], "final-fresh-read-mutation")) {
		direct_write(WRITE_FINAL_FRESH_READ_MUTATE);
	} else if (!strcmp(argv[1], "final-ftw-action-mutation")) {
		direct_write(WRITE_FINAL_FTW_ACTION_MUTATE);
	} else if (!strcmp(argv[1], "clean")) {
		expect_success();
	} else if (!strcmp(argv[1], "single-flight-reentry")) {
		install();
		reenter_on_begin = true;
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		assert(nested_status == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(begin_count == 1 && end_count == 1 && cache_ok() && !poisoned);
	} else if (!strcmp(argv[1], "end-failure")) {
		install();
		fail_end = true;
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(!poisoned && begin_count == 1 && end_count == 1 && !cache_bound);
	} else if (!strcmp(argv[1], "initialize")) {
		memset(working(), 0xff, BLOCK_SIZE);
		expect_success();
		assert(erase_count >= 3 && program_count >= 4);
	} else if (!strcmp(argv[1], "workspace-spare-body-mutation") ||
		   !strcmp(argv[1], "workspace-spare-suffix-mutation") ||
		   !strcmp(argv[1], "workspace-working-body-mutation")) {
		memset(working(), 0xff, BLOCK_SIZE);
		corrupt_workspace_spare_body =
			!strcmp(argv[1], "workspace-spare-body-mutation");
		corrupt_workspace_spare_suffix =
			!strcmp(argv[1], "workspace-spare-suffix-mutation");
		corrupt_workspace_working_body =
			!strcmp(argv[1], "workspace-working-body-mutation");
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && begin_count == 1 && end_count == 1 && !cache_bound);
		if (strcmp(argv[1], "workspace-working-body-mutation"))
			assert(!workspace_spare_marker_programmed);
		else
			assert(workspace_spare_marker_programmed &&
				!workspace_working_marker_programmed);
	} else if (!strcmp(argv[1], "dirty-tail")) {
		working()[39] = 0x7fU;
		expect_success();
	} else if (!strcmp(argv[1], "workspace-invalidate-order")) {
		working()[39] = 0x7fU;
		check_workspace_invalidate_order = true;
		expect_success();
		assert(workspace_old_invalid_marker_programmed &&
			!workspace_erased_before_invalidate);
	} else if (!strcmp(argv[1], "discard-spare")) {
		spare()[0] = 0x2bU;
		expect_success();
	} else if (!strcmp(argv[1], "recovery-nonprogress")) {
		spare()[0] = 0x2bU;
		erase_noop = true;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned);
		assert(erase_count == 2);
		assert(begin_count == 1);
		assert(end_count == 1);
		assert(!cache_bound);
	} else if (!strcmp(argv[1], "recovery-limit")) {
		alternate_recovery_plans = true;
		start_alternate_snapshot = true;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && alternate_snapshot_count == 16U);
		assert(begin_count == 1 && end_count == 1 && !cache_bound);
	} else if (!strcmp(argv[1], "abort")) {
		make_write(0xffU, 0xfcU);
		memcpy(spare(), media, BLOCK_SIZE);
		expect_success();
		assert(working()[32] == 0xf8U && erased(spare(), BLOCK_SIZE));
	} else if (!strcmp(argv[1], "abort-partial")) {
		make_write(0xffU, 0xfcU);
		memcpy(spare(), media, 777U);
		spare()[2048] = 0x7fU;
		expect_success();
		assert(working()[32] == 0xf8U && erased(spare(), BLOCK_SIZE));
	} else if (!strcmp(argv[1], "abort-spare-readback")) {
		make_write(0xffU, 0xfcU);
		memcpy(spare(), media, BLOCK_SIZE);
		corrupt_erased_spare_readback = true;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && working()[32] == 0xfcU && end_count == 1U);
	} else if (!strcmp(argv[1], "replay")) {
		make_write(0xfdU, 0xfcU);
		memcpy(spare(), media, BLOCK_SIZE);
		expect_success();
		assert(working()[72] == 0xf9U && working()[32] == 0xf8U);
	} else if (!strcmp(argv[1], "replay-spare-readback")) {
		make_write(0xfdU, 0xfcU);
		memcpy(spare(), media, BLOCK_SIZE);
		corrupt_replay_spare_readback = true;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && primary_erase_count == 0U && end_count == 1U);
	} else if (!strcmp(argv[1], "replay-primary-readback")) {
		make_write(0xfdU, 0xfcU);
		memcpy(spare(), media, BLOCK_SIZE);
		corrupt_replay_primary_readback = true;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && !ftw_destination_marker_programmed && end_count == 1U);
	} else if (!strcmp(argv[1], "complete")) {
		make_write(0xf9U, 0xfcU);
		expect_success();
		assert(working()[32] == 0xf8U);
	} else if (!strcmp(argv[1], "restore")) {
		make_workspace(spare(), 0xfeU);
		memset(working(), 0xff, BLOCK_SIZE);
		expect_success();
	} else if (!strcmp(argv[1], "restore-queue-order")) {
		make_write(0xffU, 0xfcU);
		memcpy(spare(), working(), BLOCK_SIZE);
		memset(working(), 0xff, BLOCK_SIZE);
		check_restore_queue_order = true;
		expect_success();
		assert(restore_queue_committed && !spare_erased_before_restore_queue);
	} else if (!strcmp(argv[1], "cleanup-spare-readback")) {
		memcpy(spare(), media, BLOCK_SIZE);
		corrupt_erased_spare_readback = true;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && erase_count == 2U && read_count == 5U &&
			end_count == 1U);
	} else if (!strcmp(argv[1], "malformed")) {
		working()[0] ^= 1U;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && begin_count == 1 && end_count == 1 && !cache_bound);
	} else if (!strcmp(argv[1], "read-control-mutation")) {
		install();
		mutate_control_on_read = true;
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(poisoned && fail_closed_count == 1 && begin_count == 1 &&
			end_count == 1 && !cache_bound);
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(begin_count == 1 && end_count == 1 && fail_closed_count == 1);
	} else if (!strcmp(argv[1], "fault")) {
		memset(working(), 0xff, BLOCK_SIZE);
		fail_operation = 7;
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		assert(!poisoned && begin_count == 1 && end_count == 1 && !cache_bound);
#ifdef EXECUTOR_REAL_MEDIA
	} else if (!strcmp(argv[1], "count-initialize")) {
		char count[32];
		int length;

		memset(working(), 0xff, BLOCK_SIZE);
		install();
		assert(payload_mm_authvar_executor_recover() ==
			PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
		length = snprintf(count, sizeof(count), "%u\n", operation_count);
		assert(length > 0 && (size_t)length < sizeof(count));
		assert(write(1, count, (unsigned long)length) == length);
	} else if (!strncmp(argv[1], "reset-initialize-", 17U)) {
		unsigned int cut = parse_cut(argv[1] + 17U);

		assert(cut);
		reset_initialize(cut);
#endif
	} else {
		assert(false);
	}
	return 0;
}
