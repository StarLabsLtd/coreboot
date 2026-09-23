/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_executor.h>
#include <boot/payload_mm_authvar_ftw.h>
#include <boot/payload_mm_authvar_media.h>
#include <boot/payload_mm_authvar_service.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../../src/lib/payload_mm_authvar_internal.h"
#include "payload_mm_authvar_policy_test_provider.h"
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
static uint8_t arena[131072] __aligned(__BIGGEST_ALIGNMENT__);
static bool poisoned;
static bool cache_bound;
static unsigned int begin_count;
static unsigned int end_count;
static unsigned int read_count;
static unsigned int program_count;
static unsigned int erase_count;
static unsigned int fail_closed_count;
#ifndef EXECUTOR_REAL_MEDIA
static bool fake_provider_active;
static bool fake_provider_violation;
#endif
static unsigned int cache_bind_count;
static unsigned int fail_operation;
static unsigned int operation_count;
#ifdef EXECUTOR_REAL_MEDIA
static unsigned int reset_operation;
#endif
static bool mutate_on_program;
static bool mutate_record_on_read;
static bool mutate_control_on_read;
static bool erase_noop;
static bool record_state_in_body;
static bool corrupt_body_program;
static bool record_state_marker_programmed;
static bool fail_end;
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
#ifndef EXECUTOR_REAL_MEDIA
static unsigned int tail_read_count;
#endif
#ifndef EXECUTOR_REAL_MEDIA
static unsigned int fake_erased_reads;
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

static bool fault(void)
{
	operation_count++;
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
	if (reenter_on_begin) {
		reenter_on_begin = false;
		nested_status = payload_mm_authvar_executor_recover();
	}
	if (fault())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	*generation = 1;
	*token = 2;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

enum payload_mm_authvar_media_result payload_mm_authvar_media_read(
	uint64_t generation, uint64_t token, uint32_t offset, void *buffer,
	size_t size)
{
	read_count++;
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
	if (fault() || generation != 1 || token != 2 ||
	    offset > MEDIA_SIZE || size > MEDIA_SIZE - offset)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	if (offset == 3U * BLOCK_SIZE)
		tail_read_count++;
	if (fake_erased_reads && offset >= 2U * BLOCK_SIZE) {
		memset(buffer, 0xff, size);
		fake_erased_reads--;
	} else {
		memcpy(buffer, media + offset, size);
	}
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
	if (fault() || generation != 1 || token != 2 ||
	    offset > MEDIA_SIZE || size > MEDIA_SIZE - offset)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	for (size_t i = 0; i < size; i++) {
		if ((media[offset + i] & wanted[i]) != wanted[i])
			return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
		media[offset + i] = wanted[i];
	}
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
	if (fault() || generation != 1 || token != 2 || size != BLOCK_SIZE ||
	    offset % BLOCK_SIZE || offset > MEDIA_SIZE ||
	    size > MEDIA_SIZE - offset)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
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
	if (generation != 1 || token != 2 || fault() || poisoned || fail_end)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
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
	return result == PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS ?
		PAYLOAD_MM_AUTHVAR_STATUS_SUCCESS :
		PAYLOAD_MM_AUTHVAR_STATUS_DEVICE_ERROR;
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
	if (real->marker != 0x5245414cU || fault())
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	*generation = 1;
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result real_read(const void *context,
	uint32_t offset, void *buffer, size_t size, size_t *completed)
{
	const struct real_context *real = context;

	read_count++;
	if (real->marker != 0x5245414cU || fault() || offset > MEDIA_SIZE ||
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
	if (real->marker != 0x5245414cU || fault() || offset > MEDIA_SIZE ||
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
	if (real->marker != 0x5245414cU || fault() || offset > MEDIA_SIZE ||
	    size > MEDIA_SIZE - offset)
		return PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
	memset(media + offset, 0xff, size);
	return PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS;
}

static enum payload_mm_authvar_media_result real_sync(const void *context)
{
	const struct real_context *real = context;

	return real->marker == 0x5245414cU && !fault() ?
		PAYLOAD_MM_AUTHVAR_MEDIA_SUCCESS :
		PAYLOAD_MM_AUTHVAR_MEDIA_DEVICE_ERROR;
}

static enum payload_mm_authvar_media_result real_end(const void *context)
{
	const struct real_context *real = context;

	end_count++;
	return real->marker == 0x5245414cU && !fault() ?
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

static void install(void)
{
	static struct payload_mm_authvar_executor_limits limits = {
		.maximum_store_size = REGION_SIZE,
		.maximum_name_size = 128,
		.maximum_data_size = 2048,
		.maximum_record_size = 8192,
		.maximum_records = 64,
	};

#ifdef EXECUTOR_REAL_MEDIA
	real_stack_install();
#endif

	assert(payload_mm_authvar_executor_install(arena, sizeof(arena), &limits) ==
		CB_SUCCESS);
	assert(test_policy_install() == CB_SUCCESS);
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
#endif

static bool erased(const uint8_t *bytes, size_t size)
{
	for (size_t i = 0; i < size; i++)
		if (bytes[i] != 0xffU)
			return false;
	return true;
}

int main(int argc, char **argv)
{
	assert(argc == 2);
#ifdef EXECUTOR_REAL_MEDIA
	real_media_prepare();
#endif
	make_clean();
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
