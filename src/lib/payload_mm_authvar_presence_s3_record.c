/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_s3_record.h>
#include <bootmem.h>
#include <commonlib/helpers.h>
#include <crc_byte.h>
#include <string.h>

#if !ENV_SMM && !ENV_TEST
#error "authenticated-variable S3 record is SMM-only"
#endif

#define RECORD_MAGIC 0x52335341U
#define RECORD_REVISION 1U

enum record_state {
	RECORD_CLEARED = 1,
	RECORD_ACTIVE,
	RECORD_SUSPENDED,
	RECORD_REARMING,
	RECORD_UPDATING,
	RECORD_POISONED,
};

struct payload_mm_authvar_presence_s3_record {
	uint32_t magic;
	uint32_t revision;
	uint32_t size;
	uint32_t reserved;
	uint32_t state_pair;
	uint32_t state_reserved;
	uint64_t sequence;
	uint64_t sequence_inverse;
	struct payload_mm_authvar_presence_s3_facts facts;
	struct payload_mm_authvar_presence_s3_facts facts_mirror;
	uint32_t digest;
	uint32_t digest_inverse;
} __aligned(8);

_Static_assert(offsetof(struct payload_mm_authvar_presence_s3_record, state_pair) % 4 == 0,
	"authenticated-variable S3 state alignment");
_Static_assert(sizeof(struct payload_mm_authvar_presence_s3_record) <= 4096,
	"authenticated-variable S3 record exceeds its default SMRAM region");

#if ENV_TEST
static payload_mm_authvar_presence_s3_record_test_hook_fn test_hook;
#define TEST_HOOK(point) do { if (test_hook) test_hook(point); } while (0)
#else
#define TEST_HOOK(point) do { } while (0)
#endif

static uint32_t state_pair(uint32_t state)
{
	return state | ((uint32_t)(uint16_t)~state << 16);
}

static uint32_t state_from_pair(uint32_t pair)
{
	const uint32_t state = pair & UINT16_MAX;
	return (pair >> 16) == (uint16_t)~state ? state : 0;
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t value = 0;

	while (size--)
		value |= *bytes++;
	return value == 0;
}

static void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static struct payload_mm_authvar_presence_s3_record *record_at(
	void *storage, size_t storage_size)
{
	if (!storage || storage_size < sizeof(struct payload_mm_authvar_presence_s3_record) ||
	    (uintptr_t)storage % _Alignof(struct payload_mm_authvar_presence_s3_record) ||
	    (uintptr_t)storage > UINTPTR_MAX - (storage_size - 1U))
		return NULL;
	return storage;
}

static const struct payload_mm_authvar_presence_s3_record *const_record_at(
	const void *storage, size_t storage_size)
{
	return record_at((void *)storage, storage_size);
}

static bool object_disjoint(const void *object, size_t object_size,
	const void *storage, size_t storage_size)
{
	const uintptr_t object_base = (uintptr_t)object;
	const uintptr_t storage_base = (uintptr_t)storage;
	uintptr_t object_last;
	uintptr_t storage_last;

	if (!object || !object_size || !storage || !storage_size ||
	    object_base % _Alignof(struct payload_mm_authvar_presence_s3_facts) ||
	    object_base > UINTPTR_MAX - (object_size - 1U) ||
	    storage_base > UINTPTR_MAX - (storage_size - 1U))
		return false;
	object_last = object_base + object_size - 1U;
	storage_last = storage_base + storage_size - 1U;
	return object_last < storage_base || storage_last < object_base;
}

static uint32_t facts_digest(uint64_t sequence,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	uint32_t digest = 0;
	const uint8_t *bytes = (const uint8_t *)&sequence;

	for (size_t index = 0; index < sizeof(sequence); index++)
		digest = crc32_byte(digest, bytes[index]);
	bytes = (const uint8_t *)facts;
	for (size_t index = 0; index < sizeof(*facts); index++)
		digest = crc32_byte(digest, bytes[index]);
	return digest;
}

static bool u64_ranges_disjoint(uint64_t left_base, uint64_t left_size,
	uint64_t right_base, uint64_t right_size)
{
	if (!left_size || !right_size || left_base > UINT64_MAX - (left_size - 1U) ||
	    right_base > UINT64_MAX - (right_size - 1U))
		return false;
	return left_base + left_size - 1U < right_base ||
		right_base + right_size - 1U < left_base;
}

static bool facts_valid(const struct payload_mm_authvar_presence_s3_facts *facts)
{
	return facts->revision == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_FACTS_REVISION &&
		facts->size == sizeof(*facts) &&
		payload_mm_authvar_presence_endpoint_validate(
			&facts->presence_endpoint) == CB_SUCCESS &&
		facts->presence_backing.revision ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION &&
		facts->presence_backing.size == sizeof(facts->presence_backing) &&
		facts->presence_backing.base ==
			facts->presence_endpoint.communication_base &&
		facts->presence_backing.bytes ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE &&
		facts->presence_backing.base <= UINTPTR_MAX -
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE &&
		!(facts->presence_backing.base %
			PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT) &&
		facts->presence_backing.generation ==
			facts->presence_endpoint.generation &&
		facts->presence_backing.tag == BM_MEM_RESERVED &&
		!facts->presence_backing.reserved &&
		payload_mm_authvar_presence_lifecycle_close_endpoint_validate(
			&facts->close_endpoint) == CB_SUCCESS &&
		facts->close_endpoint.generation == facts->presence_endpoint.generation &&
		facts->close_backing_base ==
			facts->close_endpoint.communication_base &&
		facts->close_backing_bytes ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_LIFECYCLE_CLOSE_BACKING_SIZE &&
		facts->close_backing_tag == BM_MEM_RESERVED &&
		!facts->close_backing_reserved &&
		u64_ranges_disjoint(facts->presence_backing.base,
			facts->presence_backing.bytes, facts->close_backing_base,
			facts->close_backing_bytes) &&
		facts->presence_terminal == PAYLOAD_MM_AUTHVAR_PRESENCE_S3_CLOSED &&
		facts->lifecycle_close_state ==
			PAYLOAD_MM_AUTHVAR_PRESENCE_S3_LIFECYCLE_CLOSE_IDLE;
}

static bool record_header_valid(
	const struct payload_mm_authvar_presence_s3_record *record)
{
	return record->magic == RECORD_MAGIC && record->revision == RECORD_REVISION &&
		record->size == sizeof(*record) && !record->reserved &&
		!record->state_reserved;
}

static bool record_content_valid(
	const struct payload_mm_authvar_presence_s3_record *record, uint32_t state)
{
	const uint32_t digest = facts_digest(record->sequence, &record->facts);

	if (record->sequence_inverse != ~record->sequence ||
	    record->digest != digest || record->digest_inverse != ~digest ||
	    memcmp(&record->facts, &record->facts_mirror, sizeof(record->facts)))
		return false;
	if (state == RECORD_CLEARED)
		return !record->sequence && zero(&record->facts, sizeof(record->facts));
	return record->sequence && facts_valid(&record->facts);
}

static void poison(struct payload_mm_authvar_presence_s3_record *record)
{
	if (record)
		__atomic_store_n(&record->state_pair, state_pair(RECORD_POISONED),
			__ATOMIC_RELEASE);
}

static bool claim(struct payload_mm_authvar_presence_s3_record *record,
	uint32_t expected_state, uint32_t claimed_state)
{
	uint32_t expected = state_pair(expected_state);

	if (!record || !record_header_valid(record)) {
		poison(record);
		return false;
	}
	/* Mutable content may only be inspected after this caller owns the state. */
	if (!__atomic_compare_exchange_n(&record->state_pair, &expected,
		state_pair(claimed_state), false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) ||
	    !record_content_valid(record, expected_state)) {
		poison(record);
		return false;
	}
	return true;
}

static bool publish_state(struct payload_mm_authvar_presence_s3_record *record,
	uint32_t claimed_state, uint32_t target_state)
{
	uint32_t expected = state_pair(claimed_state);

	if (record_content_valid(record, target_state) &&
	    __atomic_compare_exchange_n(&record->state_pair, &expected,
		state_pair(target_state), false, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
		return true;
	poison(record);
	return false;
}

static void update_content(struct payload_mm_authvar_presence_s3_record *record,
	uint64_t sequence,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	const uint32_t digest = facts_digest(sequence, facts);

	record->sequence = sequence;
	record->sequence_inverse = ~sequence;
	record->facts = *facts;
	record->facts_mirror = *facts;
	record->digest = digest;
	record->digest_inverse = ~digest;
}

static bool snapshot_facts(
	const struct payload_mm_authvar_presence_s3_facts *facts,
	struct payload_mm_authvar_presence_s3_facts *snapshot)
{
	if (!facts)
		return false;
	*snapshot = *facts;
	return facts_valid(snapshot) &&
		!memcmp(facts, snapshot, sizeof(*snapshot));
}

size_t payload_mm_authvar_presence_s3_record_size(void)
{
	return sizeof(struct payload_mm_authvar_presence_s3_record);
}

enum cb_err payload_mm_authvar_presence_s3_record_cold_clear(
	void *storage, size_t storage_size)
{
	struct payload_mm_authvar_presence_s3_record *record =
		record_at(storage, storage_size);
	struct payload_mm_authvar_presence_s3_facts empty = { 0 };

	if (!record)
		return CB_ERR;
	scrub(storage, storage_size);
	record->magic = RECORD_MAGIC;
	record->revision = RECORD_REVISION;
	record->size = sizeof(*record);
	update_content(record, 0, &empty);
	__atomic_store_n(&record->state_pair, state_pair(RECORD_CLEARED),
		__ATOMIC_RELEASE);
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_s3_record_cold_activate(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	struct payload_mm_authvar_presence_s3_record *record =
		record_at(storage, storage_size);
	struct payload_mm_authvar_presence_s3_facts snapshot;

	if (!record || !object_disjoint(facts, sizeof(*facts), storage, storage_size)) {
		poison(record);
		return CB_ERR;
	}
	if (!snapshot_facts(facts, &snapshot) ||
	    !claim(record, RECORD_CLEARED, RECORD_UPDATING)) {
		poison(record);
		return CB_ERR;
	}
	TEST_HOOK(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_COLD_ACTIVATE_CLAIMED);
	if (__atomic_load_n(&record->state_pair, __ATOMIC_ACQUIRE) !=
		state_pair(RECORD_UPDATING) ||
	    !record_content_valid(record, RECORD_CLEARED)) {
		poison(record);
		return CB_ERR;
	}
	update_content(record, 1, &snapshot);
	if (memcmp(facts, &snapshot, sizeof(snapshot))) {
		poison(record);
		return CB_ERR;
	}
	if (!publish_state(record, RECORD_UPDATING, RECORD_ACTIVE))
		return CB_ERR;
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_s3_record_suspend_seal(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	struct payload_mm_authvar_presence_s3_record *record =
		record_at(storage, storage_size);
	struct payload_mm_authvar_presence_s3_facts snapshot;
	uint64_t sequence;

	if (!record || !object_disjoint(facts, sizeof(*facts), storage, storage_size) ||
	    !snapshot_facts(facts, &snapshot) ||
	    !claim(record, RECORD_ACTIVE, RECORD_UPDATING)) {
		poison(record);
		return CB_ERR;
	}
	TEST_HOOK(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_SUSPEND_CLAIMED);
	sequence = record->sequence;
	if (__atomic_load_n(&record->state_pair, __ATOMIC_ACQUIRE) !=
		state_pair(RECORD_UPDATING) ||
	    !record_content_valid(record, RECORD_ACTIVE) ||
	    memcmp(&snapshot, &record->facts, sizeof(snapshot)) ||
	    sequence == UINT64_MAX) {
		poison(record);
		return CB_ERR;
	}
	update_content(record, sequence + 1U, &snapshot);
	if (memcmp(facts, &snapshot, sizeof(snapshot))) {
		poison(record);
		return CB_ERR;
	}
	return publish_state(record, RECORD_UPDATING, RECORD_SUSPENDED) ?
		CB_SUCCESS : CB_ERR;
}

enum cb_err payload_mm_authvar_presence_s3_record_resume_borrow(
	void *storage, size_t storage_size,
	struct payload_mm_authvar_presence_s3_facts *facts)
{
	struct payload_mm_authvar_presence_s3_record *record =
		record_at(storage, storage_size);
	struct payload_mm_authvar_presence_s3_facts snapshot;

	if (!record || !object_disjoint(facts, sizeof(*facts), storage, storage_size)) {
		poison(record);
		return CB_ERR;
	}
	scrub(facts, sizeof(*facts));
	if (!claim(record, RECORD_SUSPENDED, RECORD_REARMING))
		return CB_ERR;
	TEST_HOOK(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_BORROW_CLAIMED);
	snapshot = record->facts;
	if (!facts_valid(&snapshot) ||
	    memcmp(&snapshot, &record->facts_mirror, sizeof(snapshot)) ||
	    facts_digest(record->sequence, &snapshot) != record->digest ||
	    __atomic_load_n(&record->state_pair, __ATOMIC_ACQUIRE) !=
		state_pair(RECORD_REARMING)) {
		poison(record);
		scrub(&snapshot, sizeof(snapshot));
		return CB_ERR;
	}
	*facts = snapshot;
	scrub(&snapshot, sizeof(snapshot));
	return CB_SUCCESS;
}

enum cb_err payload_mm_authvar_presence_s3_record_rearm_commit(
	void *storage, size_t storage_size,
	const struct payload_mm_authvar_presence_s3_facts *facts)
{
	struct payload_mm_authvar_presence_s3_record *record =
		record_at(storage, storage_size);
	struct payload_mm_authvar_presence_s3_facts snapshot;
	uint64_t sequence;

	if (!record || !object_disjoint(facts, sizeof(*facts), storage, storage_size) ||
	    !snapshot_facts(facts, &snapshot) ||
	    !claim(record, RECORD_REARMING, RECORD_UPDATING)) {
		poison(record);
		return CB_ERR;
	}
	TEST_HOOK(PAYLOAD_MM_AUTHVAR_PRESENCE_S3_TEST_REARM_CLAIMED);
	sequence = record->sequence;
	if (__atomic_load_n(&record->state_pair, __ATOMIC_ACQUIRE) !=
		state_pair(RECORD_UPDATING) ||
	    !record_content_valid(record, RECORD_REARMING) ||
	    memcmp(&snapshot, &record->facts, sizeof(snapshot)) ||
	    sequence == UINT64_MAX) {
		poison(record);
		return CB_ERR;
	}
	update_content(record, sequence + 1U, &snapshot);
	if (memcmp(facts, &snapshot, sizeof(snapshot))) {
		poison(record);
		return CB_ERR;
	}
	return publish_state(record, RECORD_UPDATING, RECORD_ACTIVE) ?
		CB_SUCCESS : CB_ERR;
}

void payload_mm_authvar_presence_s3_record_poison(
	void *storage, size_t storage_size)
{
	poison(record_at(storage, storage_size));
}

#if ENV_TEST
uint32_t payload_mm_authvar_presence_s3_record_state_test(
	const void *storage, size_t storage_size)
{
	const struct payload_mm_authvar_presence_s3_record *record =
		const_record_at(storage, storage_size);

	if (!record)
		return 0;
	return state_from_pair(__atomic_load_n(&record->state_pair, __ATOMIC_ACQUIRE));
}

void payload_mm_authvar_presence_s3_record_test_hook(
	payload_mm_authvar_presence_s3_record_test_hook_fn hook)
{
	test_hook = hook;
}
#endif
