/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE)
#include <boot/payload_mm_authvar_candidate.h>
#endif
#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_service.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "payload_mm_authvar_policy_test_provider.h"
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef TEST_BLOCK_SIZE
#define TEST_BLOCK_SIZE 4096U
#endif
#define BLOCK_SIZE TEST_BLOCK_SIZE
#ifndef TEST_ERASE_SIZE
#define TEST_ERASE_SIZE BLOCK_SIZE
#endif
#define ERASE_SIZE TEST_ERASE_SIZE
#ifndef TEST_BLOCK_COUNT
#define TEST_BLOCK_COUNT 3U
#endif
#define BLOCK_COUNT TEST_BLOCK_COUNT
#define SPARE_BLOCK_COUNT (BLOCK_COUNT / 2U)
#define VARIABLE_BLOCK_COUNT (BLOCK_COUNT - SPARE_BLOCK_COUNT - 1U)
#define VARIABLE_SIZE (BLOCK_SIZE * VARIABLE_BLOCK_COUNT)
#define WORKING_OFFSET VARIABLE_SIZE
#define SPARE_OFFSET (WORKING_OFFSET + BLOCK_SIZE)
#define SPARE_SIZE (BLOCK_SIZE * SPARE_BLOCK_COUNT)
#define REGION_SIZE (BLOCK_SIZE * BLOCK_COUNT)
#define FV_HEADER_SIZE 72U
#define STORE_SIZE (VARIABLE_SIZE - FV_HEADER_SIZE)
#define POLICY_RECORD_SIZE (STORE_SIZE < 8192U ? STORE_SIZE : 8192U)
#define ARENA_SIZE (128U * 1024U)
#define TRACE_CAPACITY 16384U
#define CHILD_CUT_EXIT 77
#define RANDOM_MASK_SEED 0x7a13c9e5U

enum trace_kind {
	TRACE_BEGIN,
	TRACE_READ,
	TRACE_PROGRAM,
	TRACE_ERASE,
	TRACE_SYNC,
	TRACE_END,
	TRACE_CUT,
	TRACE_FAIL_CLOSED,
	TRACE_CACHE_INVALIDATE,
	TRACE_CACHE_BIND,
};

enum cut_kind {
	CUT_NONE,
	CUT_PROGRAM,
	CUT_ERASE,
};

enum cut_mask {
	MASK_PREFIX,
	MASK_EVEN,
	MASK_ODD,
	MASK_FIRST_LAST,
	MASK_MIDDLE_QUARTER,
	MASK_EVERY_FOURTH,
	MASK_RANDOM_BYTES,
	MASK_PARTIAL_BITS,
};

enum fault_kind {
	FAULT_NONE,
	FAULT_BEGIN,
	FAULT_READ,
	FAULT_PROGRAM,
	FAULT_ERASE,
	FAULT_SYNC,
	FAULT_END,
};

enum fault_mode {
	FAULT_MODE_DEVICE_UNCHANGED,
	FAULT_MODE_UNSUPPORTED,
	FAULT_MODE_WRITE_PROTECTED,
	FAULT_MODE_PARTIAL_DEVICE,
	FAULT_MODE_FULL_DEVICE,
	FAULT_MODE_INVALID_RESULT,
	FAULT_MODE_SHORT_ZERO,
	FAULT_MODE_SHORT_ONE,
	FAULT_MODE_SHORT_MINUS_ONE,
	FAULT_MODE_OVERCOUNT_SUCCESS,
	FAULT_MODE_REENTRY,
	FAULT_MODE_CONTEXT_MUTATION,
	FAULT_MODE_INPUT_MUTATION,
};

enum checkpoint_kind {
	CHECKPOINT_NONE,
	CHECKPOINT_QUEUE_HEADER,
	CHECKPOINT_QUEUE_RECORD,
	CHECKPOINT_PRIMARY,
	CHECKPOINT_WORKSPACE,
	CHECKPOINT_SPARE_SUFFIX,
	CHECKPOINT_WORKSPACE_SUFFIX,
};

struct trace_entry {
	uint32_t boot;
	uint32_t kind;
	uint32_t offset;
	uint32_t size;
	uint32_t completed;
	uint32_t result;
	uint64_t generation;
	uint64_t token;
	uint64_t before_digest;
	uint64_t input_digest;
	uint64_t after_digest;
};

#ifdef RECURSIVE_MUTATION_JOURNAL
#define MUTATION_JOURNAL_CAPACITY 128U
#define MUTATION_JOURNAL_SPAN 4096U
struct mutation_journal_entry {
	uint8_t before[MUTATION_JOURNAL_SPAN];
	uint8_t after[MUTATION_JOURNAL_SPAN];
	uint32_t kind;
	uint32_t offset;
	uint32_t size;
	uint32_t occurrence;
};
#endif

struct shared_state {
	uint8_t media[REGION_SIZE];
	uint8_t trace_read_image[REGION_SIZE];
	struct trace_entry trace[TRACE_CAPACITY];
	uint32_t trace_count;
	uint32_t boot;
	uint32_t program_count;
	uint32_t erase_count;
	uint32_t begin_count;
	uint32_t read_count;
	uint32_t sync_count;
	uint32_t end_count;
	uint32_t cut_kind;
	uint32_t cut_occurrence;
	uint32_t cut_bytes;
	uint32_t cut_mask;
	uint32_t fault_kind;
	uint32_t fault_occurrence;
	uint32_t fault_mode;
	uint64_t child_result;
	uint64_t retry_result;
	uint32_t callbacks_before_retry;
	uint32_t callbacks_after_retry;
	uint32_t fault_recoverable;
	uint32_t fault_bytes_changed;
	uint32_t checkpoint_kind;
	uint32_t checkpoint_armed;
	uint32_t checkpoint_injected;
	uint32_t checkpoint_marker_seen;
	uint32_t checkpoint_offset;
	uint32_t checkpoint_size;
	uint32_t checkpoint_corrupt_offset;
	uint32_t checkpoint_marker_offset;
	uint32_t workspace_spare_fe_seen;
	uint32_t suppress_diagnostics;
	uint64_t trace_generation;
	uint64_t trace_token;
	uint64_t trace_next_token;
#ifdef RECURSIVE_MUTATION_JOURNAL
	struct mutation_journal_entry mutation_journal[MUTATION_JOURNAL_CAPACITY];
	uint32_t mutation_journal_count;
#endif
};

struct backend_context {
	struct shared_state *shared;
	uint32_t marker;
};

static uint8_t arena[ARENA_SIZE] __aligned(16);
static struct backend_context backend;
static struct payload_mm_authvar_media_port media_port;
static void *communication;
static struct trace_entry reclaim_baseline[TRACE_CAPACITY];
static uint32_t reclaim_baseline_count;
static struct trace_entry direct_baseline[TRACE_CAPACITY];
static uint32_t direct_baseline_count;
static uint32_t direct_fault_counts[FAULT_END + 1U];

/* Immutable reclaim transcript from f3f0a22bdbea90e1da2726da78dee2d7ddf526b0. */
static const struct trace_entry legacy_reclaim_trace[] = {
	{ 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 8, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0x59f8663f693e42e4ULL },
	{ 0, 1, 4096, 4096, 4096, 0, 1, 1, 0, 0, 0xb345f99759ba2fa9ULL },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 1, 4128, 80, 80, 0, 1, 1, 0, 0, 0x13bfedc245d54143ULL },
	{ 0, 1, 4129, 39, 39, 0, 1, 1, 0, 0, 0xa64add8f2b6d296bULL },
	{ 0, 2, 4129, 39, 39, 0, 1, 1, 0xa64add8f2b6d296bULL,
		0x2fea6cbdf38d651eULL, 0x2fea6cbdf38d651eULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 4129, 39, 39, 0, 1, 1, 0, 0, 0x2fea6cbdf38d651eULL },
	{ 0, 1, 4129, 39, 39, 0, 1, 1, 0, 0, 0x2fea6cbdf38d651eULL },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd25d473cced67ULL },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd25d473cced67ULL },
	{ 0, 2, 4128, 1, 1, 0, 1, 1, 0x44bd25d473cced67ULL,
		0x44bd24d473ccebb4ULL, 0x44bd24d473ccebb4ULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd24d473ccebb4ULL },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd24d473ccebb4ULL },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd24d473ccebb4ULL },
	{ 0, 2, 4128, 1, 1, 0, 1, 1, 0x44bd24d473ccebb4ULL,
		0x44bd26d473ccef1aULL, 0x44bd26d473ccef1aULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd26d473ccef1aULL },
	{ 0, 1, 4169, 39, 39, 0, 1, 1, 0, 0, 0xa64add8f2b6d296bULL },
	{ 0, 2, 4169, 39, 39, 0, 1, 1, 0xa64add8f2b6d296bULL,
		0x80a4e785895a7dccULL, 0x80a4e785895a7dccULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 4169, 39, 39, 0, 1, 1, 0, 0, 0x80a4e785895a7dccULL },
	{ 0, 1, 4169, 39, 39, 0, 1, 1, 0, 0, 0x80a4e785895a7dccULL },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 3, 8192, 4096, 4096, 0, 1, 1, 0xf3bb53b198336383ULL,
		0, 0xf3bb53b198336383ULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 2, 8192, 4096, 4096, 0, 1, 1, 0xf3bb53b198336383ULL,
		0x6b8011b0de5cfa19ULL, 0x6b8011b0de5cfa19ULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0x6b8011b0de5cfa19ULL },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0x6b8011b0de5cfa19ULL },
	{ 0, 1, 4168, 1, 1, 0, 1, 1, 0, 0, 0x44bd25d473cced67ULL },
	{ 0, 1, 4168, 1, 1, 0, 1, 1, 0, 0, 0x44bd25d473cced67ULL },
	{ 0, 2, 4168, 1, 1, 0, 1, 1, 0x44bd25d473cced67ULL,
		0x44bd27d473ccf0cdULL, 0x44bd27d473ccf0cdULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 4168, 1, 1, 0, 1, 1, 0, 0, 0x44bd27d473ccf0cdULL },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0x59f8663f693e42e4ULL },
	{ 0, 3, 0, 4096, 4096, 0, 1, 1, 0x59f8663f693e42e4ULL,
		0, 0xf3bb53b198336383ULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 2, 0, 4096, 4096, 0, 1, 1, 0xf3bb53b198336383ULL,
		0x6b8011b0de5cfa19ULL, 0x6b8011b0de5cfa19ULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0x6b8011b0de5cfa19ULL },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0x6b8011b0de5cfa19ULL },
	{ 0, 1, 4168, 1, 1, 0, 1, 1, 0, 0, 0x44bd27d473ccf0cdULL },
	{ 0, 1, 4168, 1, 1, 0, 1, 1, 0, 0, 0x44bd27d473ccf0cdULL },
	{ 0, 2, 4168, 1, 1, 0, 1, 1, 0x44bd27d473ccf0cdULL,
		0x44bd23d473ccea01ULL, 0x44bd23d473ccea01ULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 4168, 1, 1, 0, 1, 1, 0, 0, 0x44bd23d473ccea01ULL },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd26d473ccef1aULL },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd26d473ccef1aULL },
	{ 0, 2, 4128, 1, 1, 0, 1, 1, 0x44bd26d473ccef1aULL,
		0x44bd22d473cce84eULL, 0x44bd22d473cce84eULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 4128, 1, 1, 0, 1, 1, 0, 0, 0x44bd22d473cce84eULL },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0x6b8011b0de5cfa19ULL },
	{ 0, 3, 8192, 4096, 4096, 0, 1, 1, 0x6b8011b0de5cfa19ULL,
		0, 0xf3bb53b198336383ULL },
	{ 0, 4, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0x6b8011b0de5cfa19ULL },
	{ 0, 1, 4096, 4096, 4096, 0, 1, 1, 0, 0, 0xacca9ff1c65fd60eULL },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 1, 0, 4096, 4096, 0, 1, 1, 0, 0, 0x6b8011b0de5cfa19ULL },
	{ 0, 1, 4096, 4096, 4096, 0, 1, 1, 0, 0, 0xacca9ff1c65fd60eULL },
	{ 0, 1, 8192, 4096, 4096, 0, 1, 1, 0, 0, 0xf3bb53b198336383ULL },
	{ 0, 9, 0, 12288, 0, 0, 1, 1, 1, 0x3c169c3a9330ca64ULL,
		0x3c169c3a9330ca64ULL },
	{ 0, 5, 0, 0, 0, 0, 1, 1, 0, 0, 0 },
};

static const uint8_t legacy_reclaim_primary[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x8d, 0x2b, 0xf1, 0xff, 0x96, 0x76, 0x8b, 0x4c,
	0xa9, 0x85, 0x27, 0x47, 0x07, 0x5b, 0x4f, 0x50,
	0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x5f, 0x46, 0x56, 0x48, 0x36, 0x0e, 0x00, 0x00,
	0x48, 0x00, 0x05, 0xba, 0x00, 0x00, 0x00, 0x02,
	0x03, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x78, 0x2c, 0xf3, 0xaa, 0x7b, 0x94, 0x9a, 0x43,
	0xa1, 0x80, 0x2e, 0x14, 0x4e, 0xc3, 0x77, 0x92,
	0xb8, 0x0f, 0x00, 0x00, 0x5a, 0xfe, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0xaa, 0x55, 0x3f, 0x00,
	0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
	0xf3, 0x2e, 0xa5, 0xc4, 0x27, 0x4e, 0xe2, 0x47,
	0x95, 0x0b, 0xfd, 0xaa, 0xb5, 0x21, 0xb8, 0x95,
	0x41, 0x00, 0x00, 0x00, 0x09, 0x08, 0x07, 0x06,
};

static const uint8_t legacy_reclaim_workspace[] = {
	0x2b, 0x29, 0x58, 0x9e, 0x68, 0x7c, 0x7d, 0x49,
	0xa0, 0xce, 0x65, 0x00, 0xfd, 0x9f, 0x1b, 0x95,
	0x2c, 0xaf, 0x2c, 0x64, 0xfe, 0xff, 0xff, 0xff,
	0xe0, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xf8, 0xff, 0xff, 0xff, 0xf3, 0x2e, 0xa5, 0xc4,
	0x27, 0x4e, 0xe2, 0x47, 0x95, 0x0b, 0xfd, 0xaa,
	0xb5, 0x21, 0xb8, 0x95, 0xff, 0xff, 0xff, 0xff,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xf9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xb8, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xe0,
};

extern char _start[];

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
static const uint8_t variable_guid[16] = {
	0xf3, 0x2e, 0xa5, 0xc4, 0x27, 0x4e, 0xe2, 0x47,
	0x95, 0x0b, 0xfd, 0xaa, 0xb5, 0x21, 0xb8, 0x95,
};
static const uint8_t variable_name[] = { 'A', 0, 0, 0 };
static const uint8_t variable_data[] = { 1, 2, 3, 4 };
static const uint8_t replacement_data[] = { 9, 8, 7, 6 };

#if CONFIG(PAYLOAD_MM_AUTHVAR_CANDIDATE)
enum payload_mm_verify_status payload_mm_sha256(const void *message,
	size_t message_size, uint8_t digest[PAYLOAD_MM_SHA256_SIZE])
{
	const uint8_t *bytes = message;

	memset(digest, 0, PAYLOAD_MM_SHA256_SIZE);
	for (size_t i = 0; i < message_size; i++)
		digest[i % PAYLOAD_MM_SHA256_SIZE] ^= bytes[i];
	return PAYLOAD_MM_VERIFY_OK;
}
#endif

static void output(int fd, const void *buffer, size_t size)
{
	ssize_t written = write(fd, buffer, size);

	if (written < 0 || (size_t)written != size)
		abort();
}

static void assertion_failed(const char *expression, const char *file, int line)
{
	char message[512];
	int length = snprintf(message, sizeof(message),
		"%s:%d: assertion failed: %s\n", file, line, expression);

	if (length > 0)
		output(2, message, (size_t)length);
	abort();
}

#define assert(c) do { if (!(c)) assertion_failed(#c, __FILE__, __LINE__); } while (0)

#ifdef RECURSIVE_MUTATION_JOURNAL
static uint32_t mutation_journal_begin(struct shared_state *shared,
	enum trace_kind kind, uint32_t offset, size_t size, uint32_t occurrence)
{
	uint32_t index = shared->mutation_journal_count++;
	struct mutation_journal_entry *entry;

	assert((kind == TRACE_PROGRAM || kind == TRACE_ERASE) &&
		index < MUTATION_JOURNAL_CAPACITY && size <= MUTATION_JOURNAL_SPAN &&
		offset <= REGION_SIZE && size <= REGION_SIZE - offset);
	entry = &shared->mutation_journal[index];
	entry->kind = kind;
	entry->offset = offset;
	entry->size = (uint32_t)size;
	entry->occurrence = occurrence;
	memcpy(entry->before, shared->media + offset, size);
	return index;
}

static void mutation_journal_end(struct shared_state *shared, uint32_t index)
{
	const struct mutation_journal_entry *entry = &shared->mutation_journal[index];

	assert(index < shared->mutation_journal_count &&
		entry->size <= MUTATION_JOURNAL_SPAN && entry->offset <= REGION_SIZE &&
		entry->size <= REGION_SIZE - entry->offset);
	memcpy(shared->mutation_journal[index].after,
		shared->media + entry->offset, entry->size);
}
#endif

static uint16_t read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
		(uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t read_le64(const uint8_t *p)
{
	uint64_t value = 0;

	for (size_t i = 0; i < sizeof(value); i++)
		value |= (uint64_t)p[i] << (8U * i);
	return value;
}

static bool bytes_are(const uint8_t *bytes, size_t size, uint8_t value)
{
	for (size_t i = 0; i < size; i++)
		if (bytes[i] != value)
			return false;
	return true;
}

static void write_le16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *p, uint32_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static void write_le64(uint8_t *p, uint64_t value)
{
	for (size_t i = 0; i < sizeof(value); i++)
		p[i] = (uint8_t)(value >> (8U * i));
}

static uint32_t independent_crc32(const uint8_t *data, size_t size)
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

static void make_clean_image(struct shared_state *shared)
{
	uint8_t header[32];
	uint16_t checksum = 0;

	memset(shared->media, 0xff, sizeof(shared->media));
	memset(shared->media, 0, FV_HEADER_SIZE);
	memcpy(shared->media + 16, fv_guid, sizeof(fv_guid));
	write_le64(shared->media + 32, REGION_SIZE);
	write_le32(shared->media + 40, 0x4856465fU);
	write_le32(shared->media + 44, 0x00000e36U);
	write_le16(shared->media + 48, FV_HEADER_SIZE);
	shared->media[55] = 2;
	write_le32(shared->media + 56, BLOCK_COUNT);
	write_le32(shared->media + 60, BLOCK_SIZE);
	for (size_t i = 0; i < FV_HEADER_SIZE; i += 2U)
		checksum = (uint16_t)(checksum + read_le16(shared->media + i));
	write_le16(shared->media + 50, (uint16_t)-checksum);
	memset(shared->media + FV_HEADER_SIZE, 0, 28U);
	memcpy(shared->media + FV_HEADER_SIZE, store_guid, sizeof(store_guid));
	write_le32(shared->media + FV_HEADER_SIZE + 16U, STORE_SIZE);
	shared->media[FV_HEADER_SIZE + 20U] = 0x5aU;
	shared->media[FV_HEADER_SIZE + 21U] = 0xfeU;

	memset(header, 0xff, sizeof(header));
	memcpy(header, work_guid, sizeof(work_guid));
	write_le64(header + 24, BLOCK_SIZE - sizeof(header));
	write_le32(header + 16, independent_crc32(header, sizeof(header)));
	memcpy(shared->media + WORKING_OFFSET, header, sizeof(header));
	shared->media[WORKING_OFFSET + 20U] = 0xfeU;
}

static uint64_t trace_digest(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint64_t digest = 1469598103934665603ULL ^ size;

	for (size_t i = 0; i < size; i++) {
		digest ^= bytes[i];
		digest *= 1099511628211ULL;
	}
	return digest;
}

static bool trace_equal(const struct trace_entry *left,
	const struct trace_entry *right)
{
	return left->boot == right->boot && left->kind == right->kind &&
		left->offset == right->offset && left->size == right->size &&
		left->completed == right->completed && left->result == right->result &&
		left->generation == right->generation && left->token == right->token &&
		left->before_digest == right->before_digest &&
		left->input_digest == right->input_digest &&
		left->after_digest == right->after_digest;
}

static void assert_legacy_reclaim_golden(const struct shared_state *shared)
{
	uint8_t expected[REGION_SIZE];

	_Static_assert(sizeof(legacy_reclaim_primary) == 168U,
		"legacy primary prefix changed");
	_Static_assert(sizeof(legacy_reclaim_workspace) == 106U,
		"legacy workspace prefix changed");
	assert(reclaim_baseline_count == ARRAY_SIZE(legacy_reclaim_trace));
	for (uint32_t i = 0; i < reclaim_baseline_count; i++)
		assert(trace_equal(&reclaim_baseline[i], &legacy_reclaim_trace[i]));

	memset(expected, 0xff, sizeof(expected));
	memcpy(expected, legacy_reclaim_primary,
		sizeof(legacy_reclaim_primary));
	memcpy(expected + WORKING_OFFSET, legacy_reclaim_workspace,
		sizeof(legacy_reclaim_workspace));
	assert(trace_digest(expected, sizeof(expected)) == 0x3c169c3a9330ca64ULL);
	assert(trace_digest(shared->media, REGION_SIZE) == 0x3c169c3a9330ca64ULL);
	assert(!memcmp(shared->media, expected, sizeof(expected)));
}

static void trace_add_digests(struct shared_state *shared, enum trace_kind kind,
	uint32_t offset, uint32_t size, uint32_t completed, uint32_t result,
	uint64_t before_digest, uint64_t input_digest, uint64_t after_digest)
{
	uint32_t index = shared->trace_count++;

	assert(index < TRACE_CAPACITY);
	shared->trace[index] = (struct trace_entry) {
		.boot = shared->boot,
		.kind = kind,
		.offset = offset,
		.size = size,
		.completed = completed,
		.result = result,
		.generation = shared->trace_generation,
		.token = shared->trace_token,
		.before_digest = before_digest,
		.input_digest = input_digest,
		.after_digest = after_digest,
	};
}

static void trace_add(struct shared_state *shared, enum trace_kind kind,
	uint32_t offset, uint32_t size, uint32_t completed, uint32_t result)
{
	trace_add_digests(shared, kind, offset, size, completed, result, 0, 0, 0);
}

static void trace_read(struct shared_state *shared, uint32_t offset, size_t size,
	const void *buffer, size_t completed,
	enum payload_mm_authvar_media_result result)
{
	size_t returned = completed < size ? completed : size;

	if (returned)
		memcpy(shared->trace_read_image + offset, buffer, returned);
	trace_add_digests(shared, TRACE_READ, offset, (uint32_t)size,
		(uint32_t)completed, (uint32_t)result, 0, 0,
		returned ? trace_digest(buffer, returned) : 0);
}

enum payload_mm_authvar_media_result
__real_payload_mm_authvar_media_fail_closed(uint64_t generation, uint64_t token);
void __real_payload_mm_authvar_media_cache_invalidate(void);
void __real_payload_mm_authvar_media_cache_bind(uint64_t generation, uint64_t token);

enum payload_mm_authvar_media_result
__wrap_payload_mm_authvar_media_fail_closed(uint64_t generation, uint64_t token)
{
	if (backend.shared) {
		uint32_t index = backend.shared->trace_count;

		trace_add_digests(backend.shared, TRACE_FAIL_CLOSED, 0, 0, 0, 0,
			generation, token, 0);
		backend.shared->trace[index].generation = generation;
		backend.shared->trace[index].token = token;
	}
	return __real_payload_mm_authvar_media_fail_closed(generation, token);
}

void __wrap_payload_mm_authvar_media_cache_invalidate(void)
{
	if (backend.shared)
		trace_add(backend.shared, TRACE_CACHE_INVALIDATE, 0, 0, 0, 0);
	__real_payload_mm_authvar_media_cache_invalidate();
}

void __wrap_payload_mm_authvar_media_cache_bind(uint64_t generation,
	uint64_t token)
{
	if (backend.shared) {
		uint32_t index = backend.shared->trace_count;

		trace_add_digests(backend.shared, TRACE_CACHE_BIND, 0, REGION_SIZE, 0,
			0, generation, trace_digest(backend.shared->trace_read_image,
				REGION_SIZE),
			trace_digest(backend.shared->media, REGION_SIZE));
		backend.shared->trace[index].generation = generation;
		backend.shared->trace[index].token = token;
	}
	__real_payload_mm_authvar_media_cache_bind(generation, token);
}

static bool backend_ready(void *context)
{
	(void)context;
	return true;
}

static bool fault_selected(struct shared_state *shared, enum fault_kind kind,
	uint32_t occurrence)
{
	return shared->fault_kind == (uint32_t)kind &&
		shared->fault_occurrence == occurrence;
}

static enum payload_mm_authvar_media_result selected_fault_result(
	const struct shared_state *shared)
{
	switch ((enum fault_mode)shared->fault_mode) {
	case FAULT_MODE_UNSUPPORTED:
		return PAYLOAD_MM_AUTHVAR_MEDIA_UNSUPPORTED;
	case FAULT_MODE_WRITE_PROTECTED:
		return PAYLOAD_MM_AUTHVAR_MEDIA_WRITE_PROTECTED;
	case FAULT_MODE_INVALID_RESULT:
		return (enum payload_mm_authvar_media_result)UINT32_MAX;
	case FAULT_MODE_DEVICE_UNCHANGED:
	case FAULT_MODE_PARTIAL_DEVICE:
	case FAULT_MODE_FULL_DEVICE:
	case FAULT_MODE_SHORT_ZERO:
	case FAULT_MODE_SHORT_ONE:
	case FAULT_MODE_SHORT_MINUS_ONE:
	case FAULT_MODE_OVERCOUNT_SUCCESS:
	case FAULT_MODE_REENTRY:
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	case FAULT_MODE_CONTEXT_MUTATION:
	case FAULT_MODE_INPUT_MUTATION:
		return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
	}
	abort();
}

static void trigger_callback_reentry(void)
{
	uint64_t generation;
	uint64_t token;

	assert(payload_mm_authvar_media_begin(&generation, &token) ==
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
}

static void mutate_callback_context(const struct backend_context *state)
{
	struct backend_context *mutable = (struct backend_context *)(uintptr_t)state;

	mutable->marker ^= 1U;
}

static bool reserve_region(void *context, uint64_t base, uint64_t size)
{
	(void)context;
	return base && size == BLOCK_SIZE;
}

static bool own_store(void *context, uint64_t offset, uint64_t size)
{
	(void)context;
	return offset == 0x600000U && size == REGION_SIZE;
}

static bool protected_storage(void *context, const void *storage, size_t size)
{
	(void)context;
	return storage && size;
}

static enum payload_mm_authvar_media_result backend_begin(const void *context,
	uint64_t *generation)
{
	const struct backend_context *state = context;

	assert(state->marker == 0x41434345U && state->shared);
	memset(state->shared->trace_read_image, 0, REGION_SIZE);
	state->shared->begin_count++;
	if (fault_selected(state->shared, FAULT_BEGIN, state->shared->begin_count)) {
		enum payload_mm_authvar_media_result result =
			selected_fault_result(state->shared);

		if (state->shared->fault_mode == FAULT_MODE_REENTRY)
			trigger_callback_reentry();
		if (state->shared->fault_mode == FAULT_MODE_CONTEXT_MUTATION) {
			mutate_callback_context(state);
			*generation = 1U;
		}
		if (result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
			state->shared->trace_generation = *generation;
			state->shared->trace_token = ++state->shared->trace_next_token;
		}
		trace_add(state->shared, TRACE_BEGIN, 0, 0, 0,
			(uint32_t)result);
		return result;
	}
	*generation = 1;
	state->shared->trace_generation = *generation;
	state->shared->trace_token = ++state->shared->trace_next_token;
	trace_add(state->shared, TRACE_BEGIN, 0, 0, 0,
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result backend_read(const void *context,
	uint32_t offset, void *buffer, size_t size, size_t *completed)
{
	const struct backend_context *state = context;

	assert(offset <= REGION_SIZE && size <= REGION_SIZE - offset);
	if (state->shared->checkpoint_armed == 2U) {
		assert(state->shared->checkpoint_corrupt_offset < REGION_SIZE);
		state->shared->media[state->shared->checkpoint_corrupt_offset] ^= 1U;
		state->shared->checkpoint_armed = 0;
		state->shared->checkpoint_injected = 1U;
	}
	state->shared->read_count++;
	if (fault_selected(state->shared, FAULT_READ, state->shared->read_count)) {
		enum payload_mm_authvar_media_result result =
			selected_fault_result(state->shared);

		if (state->shared->fault_mode == FAULT_MODE_REENTRY)
			trigger_callback_reentry();
		if (state->shared->fault_mode == FAULT_MODE_CONTEXT_MUTATION) {
			mutate_callback_context(state);
			memcpy(buffer, state->shared->media + offset, size);
			*completed = size;
			trace_read(state->shared, offset, size, buffer, size,
				PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
			return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
		}
		if (state->shared->fault_mode == FAULT_MODE_PARTIAL_DEVICE) {
			*completed = size / 2U;
			memcpy(buffer, state->shared->media + offset, *completed);
		} else if (state->shared->fault_mode == FAULT_MODE_SHORT_ZERO) {
			*completed = 0;
		} else if (state->shared->fault_mode == FAULT_MODE_SHORT_ONE) {
			*completed = size ? 1U : 0U;
			memcpy(buffer, state->shared->media + offset, *completed);
		} else if (state->shared->fault_mode == FAULT_MODE_SHORT_MINUS_ONE) {
			*completed = size ? size - 1U : 0U;
			memcpy(buffer, state->shared->media + offset, *completed);
		} else if (state->shared->fault_mode == FAULT_MODE_FULL_DEVICE) {
			*completed = size;
			memcpy(buffer, state->shared->media + offset, size);
		} else if (state->shared->fault_mode == FAULT_MODE_OVERCOUNT_SUCCESS) {
			*completed = size + 1U;
			memcpy(buffer, state->shared->media + offset, size);
		} else {
			*completed = 0;
		}
		if (state->shared->fault_mode == FAULT_MODE_SHORT_ZERO ||
		    state->shared->fault_mode == FAULT_MODE_SHORT_ONE ||
		    state->shared->fault_mode == FAULT_MODE_SHORT_MINUS_ONE ||
		    state->shared->fault_mode == FAULT_MODE_OVERCOUNT_SUCCESS)
			result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
		trace_read(state->shared, offset, size, buffer, *completed, result);
		return result;
	}
	memcpy(buffer, state->shared->media + offset, size);
	*completed = size;
	if (state->shared->checkpoint_armed == 1U &&
	    offset == state->shared->checkpoint_offset &&
	    size == state->shared->checkpoint_size)
		state->shared->checkpoint_armed = 2U;
	trace_read(state->shared, offset, size, buffer, size,
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static bool selected_cut(struct shared_state *shared, enum cut_kind kind,
	uint32_t occurrence, size_t size, size_t *completed)
{
	if (shared->cut_kind != (uint32_t)kind ||
	    occurrence != shared->cut_occurrence)
		return false;
	*completed = shared->cut_bytes < size ? shared->cut_bytes : size;
	return true;
}

static bool byte_selected(enum cut_mask mask, size_t index, size_t size,
	size_t prefix)
{
	uint32_t random;

	switch (mask) {
	case MASK_PREFIX:
		return index < prefix;
	case MASK_EVEN:
		return !(index & 1U);
	case MASK_ODD:
		return !!(index & 1U);
	case MASK_FIRST_LAST:
		return !index || index + 1U == size;
	case MASK_MIDDLE_QUARTER:
		return index >= 3U * size / 8U && index < 5U * size / 8U;
	case MASK_EVERY_FOURTH:
		return !(index & 3U);
	case MASK_RANDOM_BYTES:
		random = RANDOM_MASK_SEED ^ (uint32_t)size ^ (uint32_t)index * 0x9e3779b9U;
		random ^= random >> 16;
		random *= 0x7feb352dU;
		random ^= random >> 15;
		if (size > 1U && !index)
			return true;
		if (size > 1U && index + 1U == size)
			return false;
		return !!(random & 1U);
	case MASK_PARTIAL_BITS:
		return true;
	}
	return false;
}

static uint8_t proper_bit_subset(uint8_t bits, size_t index)
{
	uint8_t selected = 0;
	unsigned int count = 0;

	for (unsigned int bit = 0; bit < 8U; bit++) {
		if (!(bits & (uint8_t)(1U << bit)))
			continue;
		count++;
		if (((bit + (unsigned int)index + RANDOM_MASK_SEED) & 1U) == 0)
			selected |= (uint8_t)(1U << bit);
	}
	if (count < 2U)
		return 0;
	if (!selected)
		selected = bits & (uint8_t)(0U - bits);
	if (selected == bits)
		selected &= (uint8_t)(selected - 1U);
	assert(selected && selected != bits);
	return selected;
}

static unsigned int bit_count(uint8_t value)
{
	unsigned int count = 0;

	for (; value; value &= (uint8_t)(value - 1U))
		count++;
	return count;
}

static uint8_t clear_requested_except_last(uint8_t before, uint8_t wanted,
	unsigned int *remaining)
{
	uint8_t requested = before & (uint8_t)~wanted;

	for (unsigned int bit = 0; bit < 8U; bit++) {
		uint8_t mask = (uint8_t)(1U << bit);

		if (!(requested & mask))
			continue;
		if (--*remaining)
			before &= (uint8_t)~mask;
	}
	return before;
}

static size_t program_partial_fault(struct shared_state *shared,
	uint32_t offset, const uint8_t *wanted, size_t size)
{
	unsigned int remaining = 0;
	size_t changed = 0;

	for (size_t i = 0; i < size; i++)
		remaining += bit_count(shared->media[offset + i] &
			(uint8_t)~wanted[i]);
	assert(remaining);
	if (remaining == 1U) {
		for (size_t i = 0; i < size; i++) {
			uint8_t extra = shared->media[offset + i] & wanted[i];

			if (!extra)
				continue;
			shared->media[offset + i] &= (uint8_t)~(extra &
				(uint8_t)(0U - extra));
			return 1U;
		}
		assert(false);
	}
	shared->fault_recoverable = 1U;
	for (size_t i = 0; i < size; i++) {
		uint8_t before = shared->media[offset + i];

		shared->media[offset + i] = clear_requested_except_last(before,
			wanted[i], &remaining);
		changed += shared->media[offset + i] != before;
	}
	assert(changed && remaining == 0U);
	return changed;
}

static enum payload_mm_authvar_media_result backend_program(const void *context,
	uint32_t offset, const void *buffer, size_t size)
{
	const struct backend_context *state = context;
	struct shared_state *shared = state->shared;
	const uint8_t *wanted = buffer;
	size_t completed = size;
	uint64_t before_digest;
	uint64_t input_digest;
	bool cut;
#ifdef RECURSIVE_MUTATION_JOURNAL
	uint32_t journal_index;
#endif

	assert(size && size <= 4096U && offset <= REGION_SIZE &&
		size <= REGION_SIZE - offset);
	before_digest = trace_digest(shared->media + offset, size);
	input_digest = trace_digest(wanted, size);
	if (shared->checkpoint_kind == CHECKPOINT_WORKSPACE && size == 1U) {
		if (offset == SPARE_OFFSET + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET &&
		    wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_WORK_VALID)
			shared->workspace_spare_fe_seen++;
		if (offset == WORKING_OFFSET + PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET &&
		    wanted[0] == PAYLOAD_MM_AUTHVAR_FTW_WORK_INVALID &&
		    !shared->workspace_spare_fe_seen)
			shared->checkpoint_marker_seen++;
	}
	if ((shared->checkpoint_injected || shared->checkpoint_armed == 2U) &&
	    size == 1U &&
	    offset == shared->checkpoint_marker_offset)
		shared->checkpoint_marker_seen++;
	shared->program_count++;
	if (fault_selected(shared, FAULT_PROGRAM, shared->program_count)) {
		enum payload_mm_authvar_media_result result = selected_fault_result(shared);
		size_t changed = 0;

		if (shared->fault_mode == FAULT_MODE_REENTRY)
			trigger_callback_reentry();
		if (shared->fault_mode == FAULT_MODE_CONTEXT_MUTATION) {
			mutate_callback_context(state);
			changed = size;
		}
		if (shared->fault_mode == FAULT_MODE_INPUT_MUTATION) {
			uint8_t *mutable = (uint8_t *)(uintptr_t)wanted;

			mutable[0] ^= 1U;
			changed = size;
		}
		if (shared->fault_mode == FAULT_MODE_PARTIAL_DEVICE) {
			changed = program_partial_fault(shared, offset, wanted, size);
		} else if (shared->fault_mode == FAULT_MODE_FULL_DEVICE) {
			for (size_t i = 0; i < size; i++)
				shared->fault_bytes_changed |=
					shared->media[offset + i] != wanted[i];
			changed = size;
			for (size_t i = 0; i < changed; i++) {
				assert((shared->media[offset + i] & wanted[i]) == wanted[i]);
				shared->media[offset + i] &= wanted[i];
			}
		}
		trace_add_digests(shared, TRACE_PROGRAM, offset, (uint32_t)size,
			(uint32_t)changed, (uint32_t)result, before_digest, input_digest,
			trace_digest(shared->media + offset, size));
		return result;
	}
#ifdef RECURSIVE_MUTATION_JOURNAL
	journal_index = mutation_journal_begin(shared, TRACE_PROGRAM, offset, size,
		shared->program_count);
#endif
	cut = selected_cut(shared, CUT_PROGRAM, shared->program_count, size,
		&completed);
	if (cut && shared->cut_mask != MASK_PREFIX)
		completed = 0;
	for (size_t i = 0; i < size; i++) {
		uint8_t partial;

		if (cut && !byte_selected((enum cut_mask)shared->cut_mask, i, size,
			shared->cut_bytes))
			continue;
		assert((shared->media[offset + i] & wanted[i]) == wanted[i]);
		partial = proper_bit_subset((uint8_t)(shared->media[offset + i] &
			(uint8_t)~wanted[i]), i);
		if (cut && shared->cut_mask == MASK_PARTIAL_BITS) {
			if (!partial)
				continue;
			shared->media[offset + i] &= (uint8_t)~partial;
		} else {
			shared->media[offset + i] &= wanted[i];
		}
		if (cut && shared->cut_mask != MASK_PREFIX)
			completed++;
	}
#if SPARE_SIZE > VARIABLE_SIZE
	if (shared->checkpoint_kind == CHECKPOINT_SPARE_SUFFIX &&
	    offset >= SPARE_OFFSET && offset + size == SPARE_OFFSET + VARIABLE_SIZE) {
		shared->media[SPARE_OFFSET + VARIABLE_SIZE] &= 0xfeU;
		shared->checkpoint_injected = 1U;
	}
#endif
#if SPARE_SIZE > BLOCK_SIZE
	if (shared->checkpoint_kind == CHECKPOINT_WORKSPACE_SUFFIX &&
	    offset >= SPARE_OFFSET && offset + size == SPARE_OFFSET + BLOCK_SIZE) {
		shared->media[SPARE_OFFSET + BLOCK_SIZE] &= 0xfeU;
		shared->checkpoint_injected = 1U;
	}
#endif
#ifdef RECURSIVE_MUTATION_JOURNAL
	mutation_journal_end(shared, journal_index);
#endif
	trace_add_digests(shared, TRACE_PROGRAM, offset, (uint32_t)size,
		(uint32_t)completed, PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS, before_digest,
		input_digest, trace_digest(shared->media + offset, size));
	if (shared->checkpoint_kind != CHECKPOINT_NONE &&
	    !shared->checkpoint_armed && !shared->checkpoint_injected &&
	    offset == shared->checkpoint_offset && size == shared->checkpoint_size)
		shared->checkpoint_armed = 1U;
	if (cut) {
		trace_add(shared, TRACE_CUT, offset, (uint32_t)size,
			(uint32_t)completed, 0);
		_exit(CHILD_CUT_EXIT);
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result backend_erase(const void *context,
	uint32_t offset, size_t size)
{
	const struct backend_context *state = context;
	struct shared_state *shared = state->shared;
	size_t completed = size;
	uint64_t before_digest;
	bool cut;
#ifdef RECURSIVE_MUTATION_JOURNAL
	uint32_t journal_index;
#endif

	assert(size == ERASE_SIZE && !(offset % ERASE_SIZE) &&
		offset <= REGION_SIZE && size <= REGION_SIZE - offset);
	before_digest = trace_digest(shared->media + offset, size);
	shared->erase_count++;
	if (fault_selected(shared, FAULT_ERASE, shared->erase_count)) {
		enum payload_mm_authvar_media_result result = selected_fault_result(shared);
		size_t changed = 0;

		if (shared->fault_mode == FAULT_MODE_REENTRY)
			trigger_callback_reentry();
		if (shared->fault_mode == FAULT_MODE_CONTEXT_MUTATION) {
			mutate_callback_context(state);
			changed = size;
		}
		if (shared->fault_mode == FAULT_MODE_PARTIAL_DEVICE) {
			size_t last = size;

			for (size_t i = 0; i < size; i++)
				if (shared->media[offset + i] != 0xffU)
					last = i;
			if (last == size) {
				shared->media[offset] &= 0xfeU;
				changed = 1U;
				last = 0;
			} else
				shared->fault_recoverable = 1U;
			for (size_t i = 0; i < size; i++) {
				if (shared->media[offset + i] == 0xffU || i == last)
					continue;
				shared->media[offset + i] = 0xffU;
				changed++;
			}
			assert(changed);
		} else if (shared->fault_mode == FAULT_MODE_FULL_DEVICE) {
			for (size_t i = 0; i < size; i++)
				shared->fault_bytes_changed |=
					shared->media[offset + i] != 0xffU;
			changed = size;
			memset(shared->media + offset, 0xff, changed);
		}
		trace_add_digests(shared, TRACE_ERASE, offset, (uint32_t)size,
			(uint32_t)changed, (uint32_t)result, before_digest, 0,
			trace_digest(shared->media + offset, size));
		return result;
	}
#ifdef RECURSIVE_MUTATION_JOURNAL
	journal_index = mutation_journal_begin(shared, TRACE_ERASE, offset, size,
		shared->erase_count);
#endif
	cut = selected_cut(shared, CUT_ERASE, shared->erase_count, size, &completed);
	if (!cut || shared->cut_mask == MASK_PREFIX) {
		memset(shared->media + offset, 0xff, completed);
	} else {
		completed = 0;
		for (size_t i = 0; i < size; i++) {
			uint8_t partial;

			if (!byte_selected((enum cut_mask)shared->cut_mask, i, size,
				shared->cut_bytes))
				continue;
			partial = proper_bit_subset((uint8_t)~shared->media[offset + i], i);
			if (shared->cut_mask == MASK_PARTIAL_BITS) {
				if (!partial)
					continue;
				shared->media[offset + i] |= partial;
			} else {
				shared->media[offset + i] = 0xffU;
			}
			completed++;
		}
	}
#ifdef RECURSIVE_MUTATION_JOURNAL
	mutation_journal_end(shared, journal_index);
#endif
	trace_add_digests(shared, TRACE_ERASE, offset, (uint32_t)size,
		(uint32_t)completed, PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS, before_digest, 0,
		trace_digest(shared->media + offset, size));
	if (cut) {
		trace_add(shared, TRACE_CUT, offset, (uint32_t)size,
			(uint32_t)completed, 0);
		_exit(CHILD_CUT_EXIT);
	}
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result backend_sync(const void *context)
{
	const struct backend_context *state = context;

	state->shared->sync_count++;
	if (fault_selected(state->shared, FAULT_SYNC, state->shared->sync_count)) {
		enum payload_mm_authvar_media_result result =
			selected_fault_result(state->shared);

		if (state->shared->fault_mode == FAULT_MODE_REENTRY)
			trigger_callback_reentry();
		if (state->shared->fault_mode == FAULT_MODE_CONTEXT_MUTATION)
			mutate_callback_context(state);
		trace_add(state->shared, TRACE_SYNC, 0, 0, 0,
			(uint32_t)result);
		return result;
	}
	trace_add(state->shared, TRACE_SYNC, 0, 0, 0,
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result backend_end(const void *context)
{
	const struct backend_context *state = context;

	state->shared->end_count++;
	if (fault_selected(state->shared, FAULT_END, state->shared->end_count)) {
		enum payload_mm_authvar_media_result result =
			selected_fault_result(state->shared);

		if (state->shared->fault_mode == FAULT_MODE_REENTRY)
			trigger_callback_reentry();
		if (state->shared->fault_mode == FAULT_MODE_CONTEXT_MUTATION)
			mutate_callback_context(state);
		trace_add(state->shared, TRACE_END, 0, 0, 0,
			(uint32_t)result);
		state->shared->trace_generation = 0;
		state->shared->trace_token = 0;
		return result;
	}
	trace_add(state->shared, TRACE_END, 0, 0, 0,
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	state->shared->trace_generation = 0;
	state->shared->trace_token = 0;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static void install_stack(struct shared_state *shared)
{
	struct payload_mm_authvar_platform platform;
	struct payload_mm_authvar_contract contract;
	static const struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};

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
	platform.erase_size = ERASE_SIZE;
	platform.smm_entry_owned = backend_ready;
	platform.spi_writes_restricted_to_smm = backend_ready;
	platform.raw_flash_transport_absent = backend_ready;
	platform.communication_region_reserved = reserve_region;
	platform.store_region_owned_by_smm = own_store;
	assert(payload_mm_authvar_contract_build(&contract, &platform) == CB_SUCCESS);
	assert(payload_mm_authvar_authority_install(&contract, protected_storage, NULL) ==
		CB_SUCCESS);
	backend = (struct backend_context) {
		.shared = shared,
		.marker = 0x41434345U,
	};
	memset(&media_port, 0, sizeof(media_port));
	media_port.revision = PAYLOAD_MM_AUTHVAR_MEDIA_PORT_REVISION;
	media_port.size = sizeof(media_port);
	media_port.begin = backend_begin;
	media_port.read = backend_read;
	media_port.program = backend_program;
	media_port.erase = backend_erase;
	media_port.sync = backend_sync;
	media_port.end = backend_end;
	media_port.context = &backend;
	media_port.context_size = sizeof(backend);
	assert(payload_mm_authvar_media_install(&media_port) == CB_SUCCESS);
	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_SUCCESS);
	assert(test_policy_install() == CB_SUCCESS);
}

static uint64_t apply_once(bool replace)
{
	struct payload_mm_authvar_record_source source = {
		.vendor_guid = {
			0xf3, 0x2e, 0xa5, 0xc4, 0x27, 0x4e, 0xe2, 0x47,
			0x95, 0x0b, 0xfd, 0xaa, 0xb5, 0x21, 0xb8, 0x95,
		},
		.name = variable_name,
		.name_size = sizeof(variable_name),
		.data = replace ? replacement_data : variable_data,
		.data_size = sizeof(variable_data),
		.attributes = PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS,
	};

	return test_policy_apply(&source);
}

enum child_operation {
	CHILD_RECOVER,
	CHILD_RECOVER_RETRY,
	CHILD_APPLY_ADD,
	CHILD_APPLY_REPLACE,
	CHILD_APPLY_ADD_RETRY,
	CHILD_APPLY_REPLACE_RETRY,
};

static int run_child(struct shared_state *shared, enum child_operation operation)
{
	pid_t child;
	int status;

	shared->boot++;
	shared->program_count = 0;
	shared->erase_count = 0;
	shared->begin_count = 0;
	shared->read_count = 0;
	shared->sync_count = 0;
	shared->end_count = 0;
	shared->child_result = UINT64_MAX;
	shared->retry_result = UINT64_MAX;
	shared->callbacks_before_retry = 0;
	shared->callbacks_after_retry = 0;
	shared->trace_generation = 0;
	shared->trace_token = 0;
	shared->trace_next_token = 0;
#ifdef RECURSIVE_MUTATION_JOURNAL
	shared->mutation_journal_count = 0;
#endif
	child = fork();
	assert(child >= 0);
	if (!child) {
		uint64_t result;
		char message[96];
		int length;

		install_stack(shared);
		if (operation == CHILD_RECOVER || operation == CHILD_RECOVER_RETRY)
			result = payload_mm_authvar_executor_recover();
		else
			result = apply_once(operation == CHILD_APPLY_REPLACE ||
				operation == CHILD_APPLY_REPLACE_RETRY);
		shared->child_result = result;
		if (operation == CHILD_RECOVER_RETRY ||
		    operation == CHILD_APPLY_ADD_RETRY ||
		    operation == CHILD_APPLY_REPLACE_RETRY) {
			shared->callbacks_before_retry = shared->begin_count +
				shared->read_count + shared->program_count +
				shared->erase_count + shared->sync_count + shared->end_count;
			if (operation == CHILD_RECOVER_RETRY)
				shared->retry_result = payload_mm_authvar_executor_recover();
			else
				shared->retry_result = apply_once(operation ==
					CHILD_APPLY_REPLACE_RETRY);
			shared->callbacks_after_retry = shared->begin_count +
				shared->read_count + shared->program_count +
				shared->erase_count + shared->sync_count + shared->end_count;
		}
		if (result != PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS &&
		    shared->fault_kind == FAULT_NONE && !shared->suppress_diagnostics) {
			length = snprintf(message, sizeof(message),
				"acceptance child status: 0x%llx\n",
				(unsigned long long)result);
			if (length > 0)
				output(2, message, (size_t)length);
		}
		_exit(result == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS ? 0 : 1);
	}
	assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
	if (WEXITSTATUS(status) == 1 && shared->fault_kind == FAULT_NONE &&
	    !shared->suppress_diagnostics) {
		for (uint32_t i = 0; i < shared->trace_count; i++) {
			const struct trace_entry *entry = &shared->trace[i];
			char message[160];
			int length;

			if (entry->boot != shared->boot)
				continue;
			length = snprintf(message, sizeof(message),
				"trace boot=%u kind=%u off=%u size=%u done=%u result=%u\n",
				entry->boot, entry->kind, entry->offset, entry->size,
				entry->completed, entry->result);
			if (length > 0)
				output(2, message, (size_t)length);
		}
	}
	return WEXITSTATUS(status);
}

enum logical_value {
	LOGICAL_INVALID,
	LOGICAL_ABSENT,
	LOGICAL_FIRST,
	LOGICAL_SECOND,
};

static bool independent_ftw_clean(const uint8_t *media)
{
	const uint8_t *working = media + WORKING_OFFSET;
	const uint8_t *spare = media + SPARE_OFFSET;
	uint8_t header[32];
	uint16_t checksum = 0;
	size_t queue = 32U;
	uint32_t expected_crc;

	for (size_t i = 0; i < FV_HEADER_SIZE; i += 2U)
		checksum = (uint16_t)(checksum + read_le16(media + i));
	if (checksum || read_le64(media + 32) != REGION_SIZE ||
	    read_le32(media + 56) != BLOCK_COUNT ||
	    read_le32(media + 60) != BLOCK_SIZE ||
	    memcmp(working, work_guid, sizeof(work_guid)) || working[20] != 0xfeU ||
	    !bytes_are(working + 21, 3, 0xffU) ||
	    read_le64(working + 24) != BLOCK_SIZE - 32U ||
	    !bytes_are(spare, SPARE_SIZE, 0xffU))
		return false;
	memcpy(header, working, sizeof(header));
	expected_crc = read_le32(header + 16);
	memset(header + 16, 0xff, 8U);
	if (independent_crc32(header, sizeof(header)) != expected_crc)
		return false;
	while (queue < BLOCK_SIZE) {
		const uint8_t *write = working + queue;
		const uint8_t *record;

		if (bytes_are(write, BLOCK_SIZE - queue, 0xffU))
			return true;
		if (BLOCK_SIZE - queue < 80U ||
		    (write[0] != 0xfaU && write[0] != 0xf8U) ||
		    !bytes_are(write + 1U, 3U, 0xffU) ||
		    memcmp(write + 4U, variable_guid, sizeof(variable_guid)) ||
		    !bytes_are(write + 20U, 4U, 0xffU) ||
		    read_le64(write + 24) != 1U || read_le64(write + 32))
			return false;
		record = write + 40U;
		if (record[0] == 0xffU) {
			if (write[0] == 0xfaU && !bytes_are(record, 40U, 0xffU))
				return false;
		} else if (write[0] != 0xf8U || record[0] != 0xf9U ||
			   !bytes_are(record + 1U, 7U, 0xffU) ||
			   read_le64(record + 8U) ||
			   read_le64(record + 16U) != FV_HEADER_SIZE ||
			   read_le64(record + 24U) != STORE_SIZE ||
			   read_le64(record + 32U) !=
				(uint64_t)-(int64_t)SPARE_OFFSET) {
			return false;
		}
		queue += 80U;
	}
	return true;
}

static enum logical_value independent_logical_value(const uint8_t *media)
{
	const uint8_t *store = media + FV_HEADER_SIZE;
	uint32_t store_size;
	size_t offset = 28U;
	enum logical_value value = LOGICAL_ABSENT;
	bool transition = false;

	if (memcmp(media + 16, fv_guid, sizeof(fv_guid)) ||
	    read_le32(media + 40) != 0x4856465fU ||
	    read_le16(media + 48) != FV_HEADER_SIZE ||
	    memcmp(store, store_guid, sizeof(store_guid)) || store[20] != 0x5aU ||
	    store[21] != 0xfeU || read_le16(store + 22) || read_le32(store + 24))
		return LOGICAL_INVALID;
	store_size = read_le32(store + 16);
	if (store_size != STORE_SIZE)
		return LOGICAL_INVALID;
	while (offset + 60U <= store_size) {
		const uint8_t *record = store + offset;
		uint8_t state;
		uint32_t attributes;
		uint32_t name_size;
		uint32_t data_size;
		size_t name_offset;
		size_t data_offset;
		size_t end;

		if (record[0] == 0xffU && record[1] == 0xffU)
			break;
		state = record[2];
		if (read_le16(record) != 0x55aaU) {
			if (state == 0xffU)
				break;
			return LOGICAL_INVALID;
		}
		if (state != 0xffU && state != 0x7fU && state != 0x3fU && state != 0x3cU &&
		    state != 0x3eU && state != 0x3dU)
			return LOGICAL_INVALID;
		attributes = read_le32(record + 4);
		name_size = read_le32(record + 36);
		data_size = read_le32(record + 40);
		name_offset = offset + 60U;
		data_offset = (name_offset + name_size + 3U) & ~(size_t)3U;
		end = (data_offset + data_size + 3U) & ~(size_t)3U;
		if (end > store_size || end <= offset) {
			if (state == 0xffU)
				break;
			return LOGICAL_INVALID;
		}
		/* Erased state is uncommitted: stop at it and reclaim before reuse. */
		if (state == 0xffU)
			break;
		if ((state == 0x3fU || state == 0x3eU) &&
		    !memcmp(record + 44, variable_guid, sizeof(variable_guid)) &&
		    name_size == sizeof(variable_name) &&
		    !memcmp(store + name_offset, variable_name, sizeof(variable_name))) {
			enum logical_value candidate;

			if (attributes !=
			    (PAYLOAD_MM_AUTHVAR_ATTR_NON_VOLATILE |
			     PAYLOAD_MM_AUTHVAR_ATTR_BOOTSERVICE_ACCESS |
			     PAYLOAD_MM_AUTHVAR_ATTR_RUNTIME_ACCESS) ||
			    data_size != sizeof(variable_data))
				return LOGICAL_INVALID;
			if (!memcmp(store + data_offset, variable_data, sizeof(variable_data)))
				candidate = LOGICAL_FIRST;
			else if (!memcmp(store + data_offset, replacement_data,
				 sizeof(replacement_data)))
				candidate = LOGICAL_SECOND;
			else
				return LOGICAL_INVALID;
			if (state == 0x3eU) {
				if (value != LOGICAL_ABSENT)
					return LOGICAL_INVALID;
				transition = true;
			} else {
				if (value != LOGICAL_ABSENT && !transition)
					return LOGICAL_INVALID;
				transition = false;
			}
			value = candidate;
		}
		offset = end;
	}
	return value;
}

static void trace_sessions_valid(const struct shared_state *shared)
{
	uint32_t begin_attempts = 0;
	uint32_t begin = 0;
	uint32_t end = 0;
	uint32_t current_boot = 0;
	uint64_t generation = 0;
	uint64_t token = 0;
	bool failed_closed = false;
	bool failed_closed_owned = false;
	bool poison_invalidated = false;

	for (uint32_t i = 0; i < shared->trace_count; i++) {
		const struct trace_entry *entry = &shared->trace[i];

		if (entry->boot != current_boot) {
			assert(!current_boot || !failed_closed_owned ||
				(end == 1U && poison_invalidated));
			assert(!current_boot || begin == end ||
				(begin == end + 1U && i &&
				 shared->trace[i - 1U].kind == TRACE_CUT));
			current_boot = entry->boot;
			begin_attempts = 0;
			begin = 0;
			end = 0;
			generation = 0;
			token = 0;
			failed_closed = false;
			failed_closed_owned = false;
			poison_invalidated = false;
		}
		if (failed_closed_owned)
			assert((!end && entry->kind == TRACE_END) ||
				(end == 1U && !poison_invalidated &&
				 entry->kind == TRACE_CACHE_INVALIDATE));
		else if (failed_closed)
			assert(false);
		if (failed_closed_owned && entry->kind == TRACE_CACHE_INVALIDATE)
			poison_invalidated = true;
		if (entry->kind == TRACE_BEGIN) {
			begin_attempts++;
			if (entry->result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS) {
				begin++;
				assert(entry->generation && entry->token);
				generation = entry->generation;
				token = entry->token;
			} else {
				assert(!entry->generation && !entry->token);
			}
		} else if (entry->kind == TRACE_FAIL_CLOSED) {
			assert((!generation && !entry->generation && !entry->token) ||
				(entry->generation == generation && entry->token == token));
			failed_closed = true;
			failed_closed_owned = entry->generation && entry->token;
		} else if (entry->kind == TRACE_END) {
			assert(generation && token);
			assert(entry->generation == generation && entry->token == token);
			end++;
			generation = 0;
			token = 0;
		} else if (entry->kind != TRACE_CACHE_INVALIDATE) {
			assert(generation && token);
			assert(entry->generation == generation && entry->token == token);
		}
		if (entry->kind == TRACE_CACHE_BIND) {
			uint32_t offset = REGION_SIZE;
			uint32_t cursor = i;

			assert(entry->generation && entry->token &&
				entry->size == REGION_SIZE &&
				entry->input_digest && entry->after_digest &&
				entry->input_digest == entry->after_digest);
			while (offset) {
				const struct trace_entry *read;

				assert(cursor);
				read = &shared->trace[--cursor];
				assert(read->boot == entry->boot && read->kind == TRACE_READ &&
					read->offset + read->size == offset &&
					read->completed == read->size &&
					read->result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
					read->after_digest);
				offset = read->offset;
			}
			assert(i + 1U < shared->trace_count &&
				shared->trace[i + 1U].boot == entry->boot &&
				shared->trace[i + 1U].kind == TRACE_END);
		}
		if ((entry->kind == TRACE_PROGRAM || entry->kind == TRACE_ERASE) &&
		    entry->result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS &&
		    entry->completed < entry->size)
			assert(i + 1U < shared->trace_count &&
				shared->trace[i + 1U].boot == entry->boot &&
				shared->trace[i + 1U].kind == TRACE_CUT);
		if (entry->kind == TRACE_CUT)
			assert(i && shared->trace[i - 1U].boot == entry->boot &&
				(shared->trace[i - 1U].kind == TRACE_PROGRAM ||
				 shared->trace[i - 1U].kind == TRACE_ERASE));
		assert(begin_attempts <= 1U && begin <= 1U && end <= 1U && end <= begin);
	}
	assert(!failed_closed_owned || (end == 1U && poison_invalidated));
	assert(begin == end || (begin == end + 1U && shared->trace_count &&
		shared->trace[shared->trace_count - 1U].kind == TRACE_CUT));
}

enum trace_negative_case {
	TRACE_NEGATIVE_FC_MISSING_END,
	TRACE_NEGATIVE_FC_MISSING_INVALIDATE,
	TRACE_NEGATIVE_BEGIN_MISSING_END,
	TRACE_NEGATIVE_BIND_FAILED_READ,
	TRACE_NEGATIVE_BIND_PARTIAL_READ,
	TRACE_NEGATIVE_BIND_STALE_READ,
	TRACE_NEGATIVE_ZERO_OWNER_IO,
	TRACE_NEGATIVE_IDLE_FC_CALLBACK,
	TRACE_NEGATIVE_WRONG_OWNER,
};

static void trace_negative_case(enum trace_negative_case test_case)
{
	struct shared_state *test = mmap(NULL, sizeof(*test),
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	uint32_t read_result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
	uint32_t read_completed = REGION_SIZE;
	uint64_t read_digest;
	uint64_t bind_input_digest;

	assert(test != MAP_FAILED);
	memset(test, 0, sizeof(*test));
	test->boot = 1U;
	if (test_case == TRACE_NEGATIVE_ZERO_OWNER_IO) {
		trace_add(test, TRACE_BEGIN, 0, 0, 0,
			PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR);
		trace_add(test, TRACE_READ, 0, 1U, 1U,
			PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
		trace_sessions_valid(test);
		_exit(0);
	}
	if (test_case == TRACE_NEGATIVE_IDLE_FC_CALLBACK) {
		trace_add(test, TRACE_FAIL_CLOSED, 0, 0, 0, 0);
		trace_add(test, TRACE_END, 0, 0, 0,
			PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
		trace_sessions_valid(test);
		_exit(0);
	}
	test->trace_generation = 1U;
	test->trace_token = 1U;
	trace_add(test, TRACE_BEGIN, 0, 0, 0, PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	if (test_case == TRACE_NEGATIVE_WRONG_OWNER) {
		test->trace_token = 2U;
		trace_add(test, TRACE_READ, 0, 1U, 1U,
			PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
		trace_sessions_valid(test);
		_exit(0);
	}
	if (test_case == TRACE_NEGATIVE_FC_MISSING_END ||
	    test_case == TRACE_NEGATIVE_FC_MISSING_INVALIDATE) {
		trace_add(test, TRACE_FAIL_CLOSED, 0, 0, 0, 0);
		if (test_case == TRACE_NEGATIVE_FC_MISSING_INVALIDATE)
			trace_add(test, TRACE_END, 0, 0, 0,
				PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	} else if (test_case != TRACE_NEGATIVE_BEGIN_MISSING_END) {
		read_digest = trace_digest(test->media, REGION_SIZE);
		bind_input_digest = read_digest;
		if (test_case == TRACE_NEGATIVE_BIND_FAILED_READ)
			read_result = PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		if (test_case == TRACE_NEGATIVE_BIND_PARTIAL_READ)
			read_completed--;
		if (test_case == TRACE_NEGATIVE_BIND_STALE_READ)
			bind_input_digest ^= 1U;
		trace_add_digests(test, TRACE_READ, 0, REGION_SIZE, read_completed,
			read_result, 0, 0, read_digest);
		trace_add_digests(test, TRACE_CACHE_BIND, 0, REGION_SIZE, 0, 0,
			0, bind_input_digest, trace_digest(test->media, REGION_SIZE));
		trace_add(test, TRACE_END, 0, 0, 0,
			PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS);
	}
	trace_sessions_valid(test);
	_exit(0);
}

static void trace_negative_selftests(void)
{
	uint32_t executed = 0;

	for (enum trace_negative_case test_case = TRACE_NEGATIVE_FC_MISSING_END;
	     test_case <= TRACE_NEGATIVE_WRONG_OWNER; test_case++) {
		pid_t child = fork();
		int status;

		assert(child >= 0);
		if (!child) {
			(void)close(2);
			trace_negative_case(test_case);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
		executed++;
	}
	assert(executed == TRACE_NEGATIVE_WRONG_OWNER -
		TRACE_NEGATIVE_FC_MISSING_END + 1U);
}

static uint32_t run_clean_and_direct(struct shared_state *shared,
	uint32_t *program_sizes, size_t capacity)
{
	uint32_t count = 0;
	uint32_t apply_boot;

	make_clean_image(shared);
	assert(run_child(shared, CHILD_RECOVER) == 0);
	assert(independent_ftw_clean(shared->media));
	assert(independent_logical_value(shared->media) == LOGICAL_ABSENT);
	assert(run_child(shared, CHILD_APPLY_ADD) == 0);
	apply_boot = shared->boot;
	assert(independent_logical_value(shared->media) == LOGICAL_FIRST);
	assert(independent_ftw_clean(shared->media));
	for (uint32_t i = 0; i < shared->trace_count; i++) {
		const struct trace_entry *entry = &shared->trace[i];

		if (entry->boot != apply_boot)
			continue;
		assert(direct_baseline_count < ARRAY_SIZE(direct_baseline));
		direct_baseline[direct_baseline_count] = *entry;
		direct_baseline[direct_baseline_count++].boot = 0;
		if (entry->kind != TRACE_PROGRAM)
			continue;
		assert(count < capacity && entry->size);
		program_sizes[count++] = entry->size;
	}
	assert(count == shared->program_count && count);
	direct_fault_counts[FAULT_BEGIN] = shared->begin_count;
	direct_fault_counts[FAULT_READ] = shared->read_count;
	direct_fault_counts[FAULT_PROGRAM] = shared->program_count;
	direct_fault_counts[FAULT_ERASE] = shared->erase_count;
	direct_fault_counts[FAULT_SYNC] = shared->sync_count;
	direct_fault_counts[FAULT_END] = shared->end_count;
	trace_sessions_valid(shared);
	return count;
}

static void run_program_cut(struct shared_state *shared, uint32_t occurrence,
	enum cut_mask mask, uint32_t prefix)
{
	enum logical_value outcome;

	memset(shared, 0, sizeof(*shared));
	make_clean_image(shared);
	shared->cut_kind = CUT_PROGRAM;
	shared->cut_occurrence = occurrence;
	shared->cut_bytes = prefix;
	shared->cut_mask = mask;
	assert(run_child(shared, CHILD_APPLY_ADD) == CHILD_CUT_EXIT);
	shared->cut_kind = CUT_NONE;
	for (unsigned int boot = 0; boot < 4; boot++) {
		if (!run_child(shared, CHILD_RECOVER))
			break;
		assert(boot != 3U);
	}
	outcome = independent_logical_value(shared->media);
	assert(independent_ftw_clean(shared->media));
	if (outcome == LOGICAL_INVALID) {
		char message[128];
		int length = snprintf(message, sizeof(message),
			"invalid cut outcome: occurrence=%u mask=%u prefix=%u\n", occurrence,
			(unsigned int)mask, prefix);

		if (length > 0)
			output(2, message, (size_t)length);
	}
	assert(outcome == LOGICAL_ABSENT || outcome == LOGICAL_FIRST);
	if (outcome == LOGICAL_ABSENT) {
		char message[128];
		int length;

		assert(run_child(shared, CHILD_APPLY_ADD) == 0);
		assert(independent_ftw_clean(shared->media));
		length = snprintf(message, sizeof(message),
			"post-dirty apply: occurrence=%u mask=%u prefix=%u outcome=%u\n",
			occurrence, (unsigned int)mask, prefix,
			(unsigned int)independent_logical_value(shared->media));
		if (independent_logical_value(shared->media) != LOGICAL_FIRST && length > 0)
			output(2, message, (size_t)length);
		if (independent_logical_value(shared->media) != LOGICAL_FIRST) {
			for (size_t i = FV_HEADER_SIZE; i < FV_HEADER_SIZE + 128U; i += 16U) {
				length = snprintf(message, sizeof(message),
					"%04zx: %02x %02x %02x %02x %02x %02x %02x %02x"
					" %02x %02x %02x %02x %02x %02x %02x %02x\n", i,
					shared->media[i], shared->media[i + 1U],
					shared->media[i + 2U], shared->media[i + 3U],
					shared->media[i + 4U], shared->media[i + 5U],
					shared->media[i + 6U], shared->media[i + 7U],
					shared->media[i + 8U], shared->media[i + 9U],
					shared->media[i + 10U], shared->media[i + 11U],
					shared->media[i + 12U], shared->media[i + 13U],
					shared->media[i + 14U], shared->media[i + 15U]);
				if (length > 0)
					output(2, message, (size_t)length);
			}
		}
		assert(independent_logical_value(shared->media) == LOGICAL_FIRST);
	}
	trace_sessions_valid(shared);
}

static void prepare_first_value(struct shared_state *shared)
{
	memset(shared, 0, sizeof(*shared));
	make_clean_image(shared);
	assert(run_child(shared, CHILD_APPLY_ADD) == 0);
	assert(independent_logical_value(shared->media) == LOGICAL_FIRST);
	assert(independent_ftw_clean(shared->media));
	/* Model a torn append: occupied tail space must be reclaimed, not reused. */
	shared->media[FV_HEADER_SIZE + PAYLOAD_MM_AUTHVAR_STORE_HEADER_SIZE +
		((PAYLOAD_MM_AUTHVAR_RECORD_HEADER_SIZE + sizeof(variable_name) + 3U) &
		 ~(size_t)3U) + ((sizeof(variable_data) + 3U) & ~(size_t)3U)] = 0xaaU;
}

static uint32_t discover_reclaim_programs(struct shared_state *shared,
	uint32_t *program_sizes, size_t capacity, uint32_t *erase_count,
	uint32_t fault_counts[FAULT_END + 1U])
{
	uint32_t count = 0;
	uint32_t replace_boot;

	prepare_first_value(shared);
	assert(run_child(shared, CHILD_APPLY_REPLACE) == 0);
	replace_boot = shared->boot;
	assert(independent_logical_value(shared->media) == LOGICAL_SECOND);
	assert(independent_ftw_clean(shared->media));
	for (uint32_t i = 0; i < shared->trace_count; i++) {
		const struct trace_entry *entry = &shared->trace[i];

		if (entry->boot != replace_boot)
			continue;
		assert(reclaim_baseline_count < ARRAY_SIZE(reclaim_baseline));
		reclaim_baseline[reclaim_baseline_count] = *entry;
		reclaim_baseline[reclaim_baseline_count++].boot = 0;
		if (entry->kind != TRACE_PROGRAM)
			continue;
		assert(count < capacity && entry->size);
		program_sizes[count++] = entry->size;
	}
	assert(count == shared->program_count && count);
	*erase_count = shared->erase_count;
	assert(*erase_count);
	fault_counts[FAULT_BEGIN] = shared->begin_count;
	fault_counts[FAULT_READ] = shared->read_count;
	fault_counts[FAULT_PROGRAM] = shared->program_count;
	fault_counts[FAULT_ERASE] = shared->erase_count;
	fault_counts[FAULT_SYNC] = shared->sync_count;
	fault_counts[FAULT_END] = shared->end_count;
	for (enum fault_kind kind = FAULT_BEGIN; kind <= FAULT_END; kind++)
		assert(fault_counts[kind]);
	return count;
}

static enum trace_kind fault_trace_kind(enum fault_kind kind)
{
	switch (kind) {
	case FAULT_BEGIN:
		return TRACE_BEGIN;
	case FAULT_READ:
		return TRACE_READ;
	case FAULT_PROGRAM:
		return TRACE_PROGRAM;
	case FAULT_ERASE:
		return TRACE_ERASE;
	case FAULT_SYNC:
		return TRACE_SYNC;
	case FAULT_END:
		return TRACE_END;
	case FAULT_NONE:
		break;
	}
	abort();
}

static uint32_t baseline_target_size(const struct trace_entry *baseline,
	uint32_t baseline_count, enum fault_kind kind, uint32_t occurrence)
{
	enum trace_kind target = fault_trace_kind(kind);
	uint32_t seen = 0;

	for (uint32_t i = 0; i < baseline_count; i++)
		if (baseline[i].kind == (uint32_t)target && ++seen == occurrence)
			return baseline[i].size;
	abort();
}

static void assert_fault_trace_prefix(const struct shared_state *shared,
	const struct trace_entry *baseline, uint32_t baseline_count,
	enum fault_kind kind, uint32_t occurrence)
{
	enum trace_kind target = fault_trace_kind(kind);
	uint32_t target_seen = 0;
	uint32_t actual = 0;
	uint32_t expected;

	for (expected = 0; expected < baseline_count; expected++) {
		if (baseline[expected].kind == (uint32_t)target &&
		    ++target_seen == occurrence)
			break;
	}
	assert(expected < baseline_count);
	for (uint32_t i = 0; i < shared->trace_count && actual <= expected; i++) {
		const struct trace_entry *got = &shared->trace[i];
		const struct trace_entry *want;

		if (got->boot != shared->boot)
			continue;
		want = &baseline[actual];
		assert(got->kind == want->kind && got->offset == want->offset &&
			got->size == want->size);
		if (actual != expected)
			assert(got->completed == want->completed &&
				got->result == want->result &&
				got->generation == want->generation &&
				got->token == want->token &&
				got->before_digest == want->before_digest &&
				got->input_digest == want->input_digest &&
				got->after_digest == want->after_digest);
		else {
			size_t completed = 0;
			enum payload_mm_authvar_media_result result =
				selected_fault_result(shared);

			if (shared->fault_mode == FAULT_MODE_PARTIAL_DEVICE &&
			    target == TRACE_READ)
				completed = want->size / 2U;
			if (shared->fault_mode == FAULT_MODE_FULL_DEVICE &&
			    (target == TRACE_READ || target == TRACE_PROGRAM ||
			     target == TRACE_ERASE))
				completed = want->size;
			if (shared->fault_mode == FAULT_MODE_SHORT_ZERO &&
			    target == TRACE_READ) {
				completed = 0;
				result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
			}
			if (shared->fault_mode == FAULT_MODE_SHORT_ONE &&
			    target == TRACE_READ) {
				completed = want->size ? 1U : 0U;
				result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
			}
			if (shared->fault_mode == FAULT_MODE_SHORT_MINUS_ONE &&
			    target == TRACE_READ) {
				completed = want->size ? (size_t)want->size - 1U : 0U;
				result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
			}
			if (shared->fault_mode == FAULT_MODE_OVERCOUNT_SUCCESS &&
			    target == TRACE_READ) {
				completed = (size_t)want->size + 1U;
				result = PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
			}
			if (shared->fault_mode == FAULT_MODE_CONTEXT_MUTATION &&
			    (target == TRACE_READ || target == TRACE_PROGRAM ||
			     target == TRACE_ERASE))
				completed = want->size;
			if (shared->fault_mode == FAULT_MODE_INPUT_MUTATION &&
			    target == TRACE_PROGRAM)
				completed = want->size;
			if (shared->fault_mode == FAULT_MODE_PARTIAL_DEVICE &&
			    (target == TRACE_PROGRAM || target == TRACE_ERASE))
				assert(got->completed && got->completed <= got->size);
			else
				assert(got->completed == completed);
			assert(got->result == (uint32_t)result);
			if (target != TRACE_BEGIN)
				assert(got->generation == want->generation &&
					got->token == want->token);
		}
		actual++;
	}
	assert(actual == expected + 1U);
}

static void run_reclaim_program_cut(struct shared_state *shared,
	uint32_t occurrence, enum cut_mask mask, uint32_t prefix)
{
	enum logical_value value;

	prepare_first_value(shared);
	shared->cut_kind = CUT_PROGRAM;
	shared->cut_occurrence = occurrence;
	shared->cut_bytes = prefix;
	shared->cut_mask = mask;
	assert(run_child(shared, CHILD_APPLY_REPLACE) == CHILD_CUT_EXIT);
	shared->cut_kind = CUT_NONE;
	for (unsigned int boot = 0; boot < 8; boot++) {
		if (!run_child(shared, CHILD_RECOVER))
			break;
		assert(boot != 7U);
	}
	if (!independent_ftw_clean(shared->media)) {
		struct payload_mm_authvar_ftw_plan plan;
		char message[160];
		int length;
		int planned = payload_mm_authvar_ftw_plan(shared->media, REGION_SIZE,
			BLOCK_SIZE, &plan);

		length = snprintf(message, sizeof(message),
			"reclaim FTW mismatch occurrence=%u mask=%u prefix=%u plan=%d action=%u\n",
			occurrence, (unsigned int)mask, prefix, planned,
			planned == CB_SUCCESS ? (unsigned int)plan.action : UINT32_MAX);
		if (length > 0)
			output(2, message, (size_t)length);
		for (size_t i = WORKING_OFFSET; i < WORKING_OFFSET + 112U; i += 16U) {
			length = snprintf(message, sizeof(message),
				"%04zx: %02x %02x %02x %02x %02x %02x %02x %02x"
				" %02x %02x %02x %02x %02x %02x %02x %02x\n", i,
				shared->media[i], shared->media[i + 1U],
				shared->media[i + 2U], shared->media[i + 3U],
				shared->media[i + 4U], shared->media[i + 5U],
				shared->media[i + 6U], shared->media[i + 7U],
				shared->media[i + 8U], shared->media[i + 9U],
				shared->media[i + 10U], shared->media[i + 11U],
				shared->media[i + 12U], shared->media[i + 13U],
				shared->media[i + 14U], shared->media[i + 15U]);
			if (length > 0)
				output(2, message, (size_t)length);
		}
	}
	assert(independent_ftw_clean(shared->media));
	value = independent_logical_value(shared->media);
	assert(value == LOGICAL_FIRST || value == LOGICAL_SECOND);
	if (value == LOGICAL_FIRST) {
		assert(run_child(shared, CHILD_APPLY_REPLACE) == 0);
		assert(independent_ftw_clean(shared->media));
		assert(independent_logical_value(shared->media) == LOGICAL_SECOND);
	}
	trace_sessions_valid(shared);
}

static void run_reclaim_erase_cut(struct shared_state *shared,
	uint32_t occurrence, enum cut_mask mask, uint32_t prefix)
{
	enum logical_value value;

	prepare_first_value(shared);
	shared->cut_kind = CUT_ERASE;
	shared->cut_occurrence = occurrence;
	shared->cut_bytes = prefix;
	shared->cut_mask = mask;
	assert(run_child(shared, CHILD_APPLY_REPLACE) == CHILD_CUT_EXIT);
	shared->cut_kind = CUT_NONE;
	for (unsigned int boot = 0; boot < 8; boot++) {
		if (!run_child(shared, CHILD_RECOVER))
			break;
		assert(boot != 7U);
	}
	assert(independent_ftw_clean(shared->media));
	value = independent_logical_value(shared->media);
	assert(value == LOGICAL_FIRST || value == LOGICAL_SECOND);
	if (value == LOGICAL_FIRST) {
		assert(run_child(shared, CHILD_APPLY_REPLACE) == 0);
		assert(independent_ftw_clean(shared->media));
		assert(independent_logical_value(shared->media) == LOGICAL_SECOND);
	}
	trace_sessions_valid(shared);
}

static uint64_t expected_fault_status(enum fault_kind kind, enum fault_mode mode)
{
	if (mode == FAULT_MODE_WRITE_PROTECTED && kind != FAULT_READ &&
	    kind != FAULT_SYNC && kind != FAULT_END)
		return PAYLOAD_MM_AUTHVAR_STATUS_WRITE_PROTECTED;
	if (mode == FAULT_MODE_UNSUPPORTED &&
	    (kind == FAULT_PROGRAM || kind == FAULT_ERASE))
		return PAYLOAD_MM_AUTHVAR_STATUS_UNSUPPORTED;
	return PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
}

static void run_reclaim_fault(struct shared_state *shared,
	enum fault_kind kind, uint32_t occurrence, enum fault_mode mode)
{
	enum logical_value value;

	prepare_first_value(shared);
	shared->fault_kind = kind;
	shared->fault_occurrence = occurrence;
	shared->fault_mode = mode;
	{
		int child_status = run_child(shared, CHILD_APPLY_REPLACE);

		if (child_status != 1) {
			char message[128];
			int length = snprintf(message, sizeof(message),
				"fault not observed kind=%u occurrence=%u status=%d counts="
				"%u/%u/%u/%u/%u/%u\n", (unsigned int)kind,
				occurrence, child_status, shared->begin_count,
				shared->read_count, shared->program_count, shared->erase_count,
				shared->sync_count, shared->end_count);

			if (length > 0)
				output(2, message, (size_t)length);
		}
		assert(child_status == 1);
		assert(shared->child_result == expected_fault_status(kind, mode));
		assert_fault_trace_prefix(shared, reclaim_baseline,
			reclaim_baseline_count, kind, occurrence);
	}
	shared->fault_kind = FAULT_NONE;
	shared->suppress_diagnostics = 1U;
	for (unsigned int boot = 0; boot < 8; boot++) {
		if (!run_child(shared, CHILD_RECOVER))
			break;
		assert(boot != 7U);
	}
	assert(independent_ftw_clean(shared->media));
	value = independent_logical_value(shared->media);
	assert(value == LOGICAL_FIRST || value == LOGICAL_SECOND);
	if (value == LOGICAL_FIRST) {
		assert(run_child(shared, CHILD_APPLY_REPLACE) == 0);
		assert(independent_ftw_clean(shared->media));
		assert(independent_logical_value(shared->media) == LOGICAL_SECOND);
	}
	shared->suppress_diagnostics = 0;
	trace_sessions_valid(shared);
}

static void run_direct_fault(struct shared_state *shared,
	enum fault_kind kind, uint32_t occurrence, enum fault_mode mode)
{
	enum logical_value value;

	memset(shared, 0, sizeof(*shared));
	make_clean_image(shared);
	shared->fault_kind = kind;
	shared->fault_occurrence = occurrence;
	shared->fault_mode = mode;
	assert(run_child(shared, CHILD_APPLY_ADD) == 1);
	assert(shared->child_result == expected_fault_status(kind, mode));
	assert_fault_trace_prefix(shared, direct_baseline, direct_baseline_count,
		kind, occurrence);
	shared->fault_kind = FAULT_NONE;
	shared->suppress_diagnostics = 1U;
	for (unsigned int boot = 0; boot < 8U; boot++) {
		if (!run_child(shared, CHILD_RECOVER))
			break;
		assert(boot != 7U);
	}
	assert(independent_ftw_clean(shared->media));
	value = independent_logical_value(shared->media);
	assert(value == LOGICAL_ABSENT || value == LOGICAL_FIRST);
	if (value == LOGICAL_ABSENT) {
		assert(run_child(shared, CHILD_APPLY_ADD) == 0);
		assert(independent_ftw_clean(shared->media));
		assert(independent_logical_value(shared->media) == LOGICAL_FIRST);
	}
	shared->suppress_diagnostics = 0;
	trace_sessions_valid(shared);
}

static void run_poison_fault(struct shared_state *shared, bool replace,
	enum fault_kind kind, uint32_t occurrence, enum fault_mode mode)
{
	enum logical_value value;

	if (replace)
		prepare_first_value(shared);
	else {
		memset(shared, 0, sizeof(*shared));
		make_clean_image(shared);
	}
	shared->fault_kind = kind;
	shared->fault_occurrence = occurrence;
	shared->fault_mode = mode;
	{
		int child_status = run_child(shared, replace ? CHILD_APPLY_REPLACE_RETRY :
			CHILD_APPLY_ADD_RETRY);

		if (child_status != 1) {
			char message[160];
			int length = snprintf(message, sizeof(message),
				"poison fault not observed replace=%u kind=%u occurrence=%u mode=%u"
				" first=0x%llx retry=0x%llx callbacks=%u/%u\n", replace,
				(unsigned int)kind, occurrence, (unsigned int)mode,
				(unsigned long long)shared->child_result,
				(unsigned long long)shared->retry_result,
				shared->callbacks_before_retry, shared->callbacks_after_retry);

			if (length > 0)
				output(2, message, (size_t)length);
		}
		assert(child_status == 1);
	}
	assert(shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(shared->retry_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(shared->callbacks_before_retry == shared->callbacks_after_retry);
	assert_fault_trace_prefix(shared,
		replace ? reclaim_baseline : direct_baseline,
		replace ? reclaim_baseline_count : direct_baseline_count,
		kind, occurrence);
	shared->fault_kind = FAULT_NONE;
	if (mode == FAULT_MODE_PARTIAL_DEVICE && !shared->fault_recoverable) {
		shared->suppress_diagnostics = 1U;
		int recovery_status = run_child(shared, CHILD_RECOVER);

		assert(recovery_status == 0 || recovery_status == 1);
		if (recovery_status)
			assert(shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		else {
			assert(independent_ftw_clean(shared->media));
			value = independent_logical_value(shared->media);
			assert(value == LOGICAL_ABSENT || value == LOGICAL_FIRST ||
				value == LOGICAL_SECOND);
		}
		trace_sessions_valid(shared);
		shared->suppress_diagnostics = 0;
		return;
	}
	shared->suppress_diagnostics = 1U;
	for (unsigned int boot = 0; boot < 8U; boot++) {
		if (!run_child(shared, CHILD_RECOVER))
			break;
		assert(boot != 7U);
	}
	assert(independent_ftw_clean(shared->media));
	value = independent_logical_value(shared->media);
	if (replace) {
		assert(value == LOGICAL_FIRST || value == LOGICAL_SECOND);
		if (value == LOGICAL_FIRST)
			assert(run_child(shared, CHILD_APPLY_REPLACE) == 0);
		assert(independent_logical_value(shared->media) == LOGICAL_SECOND);
	} else {
		assert(value == LOGICAL_ABSENT || value == LOGICAL_FIRST);
		if (value == LOGICAL_ABSENT)
			assert(run_child(shared, CHILD_APPLY_ADD) == 0);
		assert(independent_logical_value(shared->media) == LOGICAL_FIRST);
	}
	assert(independent_ftw_clean(shared->media));
	shared->suppress_diagnostics = 0;
	trace_sessions_valid(shared);
}

static void run_full_error_fault(struct shared_state *shared, bool replace,
	enum fault_kind kind, uint32_t occurrence)
{
	int child_status;

	if (replace)
		prepare_first_value(shared);
	else {
		memset(shared, 0, sizeof(*shared));
		make_clean_image(shared);
	}
	shared->fault_kind = kind;
	shared->fault_occurrence = occurrence;
	shared->fault_mode = FAULT_MODE_FULL_DEVICE;
	shared->suppress_diagnostics = 1U;
	child_status = run_child(shared, replace ? CHILD_APPLY_REPLACE :
		CHILD_APPLY_ADD);
	if (!shared->fault_bytes_changed) {
		assert(child_status == 1 &&
			shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
		if (replace)
			run_reclaim_fault(shared, kind, occurrence,
				FAULT_MODE_FULL_DEVICE);
		else
			run_direct_fault(shared, kind, occurrence, FAULT_MODE_FULL_DEVICE);
		return;
	}
	assert(child_status == 0 &&
		shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS);
	assert_fault_trace_prefix(shared,
		replace ? reclaim_baseline : direct_baseline,
		replace ? reclaim_baseline_count : direct_baseline_count,
		kind, occurrence);
	assert(independent_ftw_clean(shared->media));
	assert(independent_logical_value(shared->media) ==
		(replace ? LOGICAL_SECOND : LOGICAL_FIRST));
	trace_sessions_valid(shared);
}

static void run_checkpoint_corruption(struct shared_state *shared,
	enum checkpoint_kind kind)
{
	uint32_t queue = WORKING_OFFSET + PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE;

	prepare_first_value(shared);
	shared->checkpoint_kind = kind;
	shared->suppress_diagnostics = 1U;
	switch (kind) {
	case CHECKPOINT_QUEUE_HEADER:
		shared->checkpoint_offset = queue + 1U;
		shared->checkpoint_size = PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE - 1U;
		shared->checkpoint_corrupt_offset = queue + 1U;
		shared->checkpoint_marker_offset = queue;
		break;
	case CHECKPOINT_QUEUE_RECORD:
		shared->checkpoint_offset = queue +
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE + 1U;
		shared->checkpoint_size = PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE - 1U;
		shared->checkpoint_corrupt_offset = shared->checkpoint_offset;
		shared->checkpoint_marker_offset = queue +
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;
		break;
	case CHECKPOINT_PRIMARY:
		shared->checkpoint_offset = 0;
		shared->checkpoint_size = BLOCK_SIZE;
		shared->checkpoint_corrupt_offset = 0;
		shared->checkpoint_marker_offset = queue +
			PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;
		break;
	case CHECKPOINT_WORKSPACE:
	case CHECKPOINT_SPARE_SUFFIX:
	case CHECKPOINT_WORKSPACE_SUFFIX:
	case CHECKPOINT_NONE:
		abort();
	}
	assert(run_child(shared, CHILD_APPLY_REPLACE_RETRY) == 1);
	assert(shared->checkpoint_injected == 1U &&
		shared->checkpoint_marker_seen == 0U);
	assert(shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		shared->retry_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(shared->callbacks_before_retry == shared->callbacks_after_retry);
	trace_sessions_valid(shared);
}

static void __maybe_unused run_spare_suffix_overrun(struct shared_state *shared)
{
	uint32_t queue = WORKING_OFFSET + PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE;

	assert(SPARE_SIZE > VARIABLE_SIZE);
	prepare_first_value(shared);
	shared->checkpoint_kind = CHECKPOINT_SPARE_SUFFIX;
	shared->checkpoint_marker_offset = queue +
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE;
	shared->suppress_diagnostics = 1U;
	assert(run_child(shared, CHILD_APPLY_REPLACE_RETRY) == 1);
	assert(shared->checkpoint_injected == 1U &&
		shared->checkpoint_marker_seen == 0U);
	assert(shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		shared->retry_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(shared->callbacks_before_retry == shared->callbacks_after_retry);
	trace_sessions_valid(shared);
}

static void make_exhausted_workspace(struct shared_state *shared);

static void __maybe_unused run_workspace_suffix_overrun(
	struct shared_state *shared)
{
	assert(SPARE_SIZE > BLOCK_SIZE);
	make_exhausted_workspace(shared);
	shared->checkpoint_kind = CHECKPOINT_WORKSPACE_SUFFIX;
	shared->checkpoint_marker_offset = SPARE_OFFSET +
		PAYLOAD_MM_AUTHVAR_FTW_WORK_STATE_OFFSET;
	shared->suppress_diagnostics = 1U;
	assert(run_child(shared, CHILD_RECOVER_RETRY) == 1);
	assert(shared->checkpoint_injected == 1U &&
		shared->checkpoint_marker_seen == 0U);
	assert(shared->child_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR &&
		shared->retry_result == PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR);
	assert(shared->callbacks_before_retry == shared->callbacks_after_retry);
	trace_sessions_valid(shared);
}

static void make_exhausted_workspace(struct shared_state *shared)
{
	uint8_t *workspace;
	size_t entry_size = PAYLOAD_MM_AUTHVAR_FTW_WRITE_HEADER_SIZE +
		PAYLOAD_MM_AUTHVAR_FTW_WRITE_RECORD_SIZE;

	memset(shared, 0, sizeof(*shared));
	make_clean_image(shared);
	workspace = shared->media + WORKING_OFFSET;
	for (size_t offset = PAYLOAD_MM_AUTHVAR_FTW_WORK_HEADER_SIZE;
	     offset + entry_size <= BLOCK_SIZE; offset += entry_size) {
		uint8_t *header = workspace + offset;

		header[0] = PAYLOAD_MM_AUTHVAR_FTW_HEADER_COMPLETE;
		memcpy(header + 4U, payload_mm_authvar_ftw_coreboot_caller_guid, 16U);
		write_le64(header + 24U, 1U);
		write_le64(header + 32U, 0U);
	}
}

static void run_workspace_checkpoint(struct shared_state *shared)
{
	struct payload_mm_authvar_ftw_plan plan;

	make_exhausted_workspace(shared);
	assert(payload_mm_authvar_ftw_plan(shared->media, REGION_SIZE, BLOCK_SIZE,
		&plan) == CB_SUCCESS);
	assert(plan.action == PAYLOAD_MM_AUTHVAR_FTW_RECLAIM_WORKSPACE);
	shared->checkpoint_kind = CHECKPOINT_WORKSPACE;
	assert(run_child(shared, CHILD_RECOVER) == 0);
	assert(shared->workspace_spare_fe_seen == 1U &&
		shared->checkpoint_marker_seen == 0U);
	assert(independent_ftw_clean(shared->media));
	trace_sessions_valid(shared);
}

int main(int argc, char **argv)
{
	struct shared_state *shared = mmap(NULL, sizeof(*shared),
		PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	uint32_t program_sizes[32];
	uint32_t program_count;
	uint32_t reclaim_program_sizes[64];
	uint32_t reclaim_program_count;
	uint32_t reclaim_erase_count;
	uint32_t fault_counts[FAULT_END + 1U] = { 0 };
	bool fault_only = argc == 2 && !strcmp(argv[1], "fault-only");
	bool golden_only = argc == 2 && !strcmp(argv[1], "golden-only");

	assert(shared != MAP_FAILED && (argc == 1 || fault_only || golden_only));
	trace_negative_selftests();
	memset(shared, 0, sizeof(*shared));
	program_count = run_clean_and_direct(shared, program_sizes,
		ARRAY_SIZE(program_sizes));
	for (uint32_t occurrence = 1;
	     !fault_only && !golden_only && occurrence <= program_count;
	     occurrence++)
		for (uint32_t prefix = 0; prefix <= program_sizes[occurrence - 1U];
		     prefix++)
			run_program_cut(shared, occurrence, MASK_PREFIX, prefix);
	for (uint32_t occurrence = 1;
	     !fault_only && !golden_only && occurrence <= program_count;
	     occurrence++)
		for (enum cut_mask mask = MASK_EVEN; mask <= MASK_PARTIAL_BITS; mask++)
			run_program_cut(shared, occurrence, mask, 0);
	reclaim_program_count = discover_reclaim_programs(shared,
		reclaim_program_sizes,
		ARRAY_SIZE(reclaim_program_sizes), &reclaim_erase_count, fault_counts);
	assert_legacy_reclaim_golden(shared);
	if (golden_only) {
		assert(munmap(shared, sizeof(*shared)) == 0);
		return 0;
	}
	for (uint32_t occurrence = 1; !fault_only && occurrence <= reclaim_program_count;
	     occurrence++)
		for (uint32_t prefix = 0;
		     prefix <= reclaim_program_sizes[occurrence - 1U]; prefix++)
			run_reclaim_program_cut(shared, occurrence, MASK_PREFIX, prefix);
	for (uint32_t occurrence = 1; !fault_only && occurrence <= reclaim_program_count;
	     occurrence++)
		for (enum cut_mask mask = MASK_EVEN; mask <= MASK_PARTIAL_BITS; mask++)
			run_reclaim_program_cut(shared, occurrence, mask, 0);
	for (uint32_t occurrence = 1; !fault_only && occurrence <= reclaim_erase_count;
	     occurrence++)
		for (uint32_t prefix = 0; prefix <= ERASE_SIZE; prefix++)
			run_reclaim_erase_cut(shared, occurrence, MASK_PREFIX, prefix);
	for (uint32_t occurrence = 1; !fault_only && occurrence <= reclaim_erase_count;
	     occurrence++)
		for (enum cut_mask mask = MASK_EVEN; mask <= MASK_PARTIAL_BITS; mask++)
			run_reclaim_erase_cut(shared, occurrence, mask, 0);
	for (enum fault_kind kind = FAULT_BEGIN; kind <= FAULT_END; kind++)
		for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[kind];
		     occurrence++)
			run_direct_fault(shared, kind, occurrence,
				FAULT_MODE_DEVICE_UNCHANGED);
	for (enum fault_kind kind = FAULT_BEGIN; kind <= FAULT_END; kind++)
		for (uint32_t occurrence = 1; occurrence <= fault_counts[kind];
		     occurrence++)
			run_reclaim_fault(shared, kind, occurrence,
				FAULT_MODE_DEVICE_UNCHANGED);
	for (enum fault_kind kind = FAULT_PROGRAM; kind <= FAULT_ERASE; kind++) {
		for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[kind];
		     occurrence++) {
			run_direct_fault(shared, kind, occurrence, FAULT_MODE_UNSUPPORTED);
			run_direct_fault(shared, kind, occurrence,
				FAULT_MODE_WRITE_PROTECTED);
		}
		for (uint32_t occurrence = 1; occurrence <= fault_counts[kind];
		     occurrence++) {
			run_reclaim_fault(shared, kind, occurrence, FAULT_MODE_UNSUPPORTED);
			run_reclaim_fault(shared, kind, occurrence,
				FAULT_MODE_WRITE_PROTECTED);
		}
	}
	for (enum fault_mode mode = FAULT_MODE_UNSUPPORTED;
	     mode <= FAULT_MODE_WRITE_PROTECTED; mode++) {
		for (uint32_t occurrence = 1;
		     occurrence <= direct_fault_counts[FAULT_BEGIN]; occurrence++)
			run_direct_fault(shared, FAULT_BEGIN, occurrence, mode);
		for (uint32_t occurrence = 1; occurrence <= fault_counts[FAULT_BEGIN];
		     occurrence++)
			run_reclaim_fault(shared, FAULT_BEGIN, occurrence, mode);
	}
	for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[FAULT_PROGRAM];
	     occurrence++)
		run_poison_fault(shared, false, FAULT_PROGRAM, occurrence,
			FAULT_MODE_PARTIAL_DEVICE);
	for (uint32_t occurrence = 1; occurrence <= fault_counts[FAULT_PROGRAM];
	     occurrence++)
		run_poison_fault(shared, true, FAULT_PROGRAM, occurrence,
			FAULT_MODE_PARTIAL_DEVICE);
	for (uint32_t occurrence = 1; occurrence <= fault_counts[FAULT_ERASE];
	     occurrence++)
		run_poison_fault(shared, true, FAULT_ERASE, occurrence,
			FAULT_MODE_PARTIAL_DEVICE);
	for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[FAULT_PROGRAM];
	     occurrence++)
		run_full_error_fault(shared, false, FAULT_PROGRAM, occurrence);
	for (uint32_t occurrence = 1; occurrence <= fault_counts[FAULT_PROGRAM];
	     occurrence++)
		run_full_error_fault(shared, true, FAULT_PROGRAM, occurrence);
	for (uint32_t occurrence = 1; occurrence <= fault_counts[FAULT_ERASE];
	     occurrence++)
		run_full_error_fault(shared, true, FAULT_ERASE, occurrence);
	for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[FAULT_PROGRAM];
	     occurrence++)
		run_poison_fault(shared, false, FAULT_PROGRAM, occurrence,
			FAULT_MODE_INPUT_MUTATION);
	for (uint32_t occurrence = 1; occurrence <= fault_counts[FAULT_PROGRAM];
	     occurrence++)
		run_poison_fault(shared, true, FAULT_PROGRAM, occurrence,
			FAULT_MODE_INPUT_MUTATION);
	for (enum fault_kind kind = FAULT_BEGIN; kind <= FAULT_END; kind++) {
		for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[kind];
		     occurrence++)
			run_poison_fault(shared, false, kind, occurrence,
				FAULT_MODE_INVALID_RESULT);
		for (uint32_t occurrence = 1; occurrence <= fault_counts[kind];
		     occurrence++)
			run_poison_fault(shared, true, kind, occurrence,
				FAULT_MODE_INVALID_RESULT);
	}
	for (enum fault_kind kind = FAULT_BEGIN; kind <= FAULT_END; kind++) {
		for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[kind];
		     occurrence++)
			run_poison_fault(shared, false, kind, occurrence,
				FAULT_MODE_CONTEXT_MUTATION);
		for (uint32_t occurrence = 1; occurrence <= fault_counts[kind];
		     occurrence++)
			run_poison_fault(shared, true, kind, occurrence,
				FAULT_MODE_CONTEXT_MUTATION);
	}
	for (enum fault_kind kind = FAULT_BEGIN; kind <= FAULT_END; kind++) {
		for (uint32_t occurrence = 1; occurrence <= direct_fault_counts[kind];
		     occurrence++)
			run_poison_fault(shared, false, kind, occurrence,
				FAULT_MODE_REENTRY);
		for (uint32_t occurrence = 1; occurrence <= fault_counts[kind];
		     occurrence++)
			run_poison_fault(shared, true, kind, occurrence,
				FAULT_MODE_REENTRY);
	}
	for (enum fault_mode mode = FAULT_MODE_SHORT_ZERO;
	     mode <= FAULT_MODE_OVERCOUNT_SUCCESS; mode++) {
		for (uint32_t occurrence = 1;
		     occurrence <= direct_fault_counts[FAULT_READ]; occurrence++) {
			if (mode == FAULT_MODE_SHORT_ONE &&
			    baseline_target_size(direct_baseline, direct_baseline_count,
				FAULT_READ, occurrence) <= 1U)
				continue;
			run_direct_fault(shared, FAULT_READ, occurrence, mode);
		}
		for (uint32_t occurrence = 1; occurrence <= fault_counts[FAULT_READ];
		     occurrence++) {
			if (mode == FAULT_MODE_SHORT_ONE &&
			    baseline_target_size(reclaim_baseline, reclaim_baseline_count,
				FAULT_READ, occurrence) <= 1U)
				continue;
			run_reclaim_fault(shared, FAULT_READ, occurrence, mode);
		}
	}
	run_checkpoint_corruption(shared, CHECKPOINT_QUEUE_HEADER);
	run_checkpoint_corruption(shared, CHECKPOINT_QUEUE_RECORD);
	run_checkpoint_corruption(shared, CHECKPOINT_PRIMARY);
	run_workspace_checkpoint(shared);
	assert(munmap(shared, sizeof(*shared)) == 0);
	static const char success[] =
		"Payload-MM authenticated-variable executor acceptance checkpoint: PASS\n";

	output(1, success, sizeof(success) - 1U);
	return 0;
}
