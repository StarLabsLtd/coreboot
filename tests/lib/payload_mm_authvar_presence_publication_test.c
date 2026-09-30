/* SPDX-License-Identifier: GPL-2.0-only */

#include <assert.h>
#include <boot/payload_mm_authvar_presence_publication.h>
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER) && CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
#include <boot/payload_mm_authvar_presence_tuple_sender.h>
#endif
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#undef assert
#define assert(condition) do { if (!(condition)) abort(); } while (0)

static bool required;
static bool composition_ok;
static enum cb_err reserve_status;
static enum cb_err compose_status;
static enum cb_err take_status;
static unsigned int reserve_calls;
static unsigned int compose_calls;
static unsigned int take_calls;
static unsigned int complete_calls;
static unsigned int abort_calls;
static unsigned int sender_close_calls;
static unsigned int record_calls;
static unsigned int required_calls;
static unsigned int composition_calls;
static unsigned int scrubbed_compositions;
static unsigned int scrubbed_endpoints;
static unsigned int scrubbed_receipts;
static unsigned int sequence;
static unsigned int take_sequence;
static unsigned int record_sequence;
static bool reenter_required;
static bool reenter_composition;
static bool reenter_compose;
static bool reenter_take;
static bool mutate_composition_table;
static bool mutate_take_header;
static bool mutate_take_record_tail;
static bool mutate_take_publication_state;
static bool complete_error;
static struct lb_header *active_header;
static uintptr_t active_table_end;
static u8 table_storage[sizeof(struct lb_header) + 128 +
			sizeof(struct lb_authvar_presence_endpoint)]
	__aligned(16);
static unsigned int race_mode;
static unsigned int race_phase;
static enum cb_err owner_status;
static enum cb_err contender_status;

#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER) && CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
enum cb_err payload_mm_authvar_presence_tuple_sender_reserve(void)
{
	return CB_SUCCESS;
}

void payload_mm_authvar_presence_tuple_sender_close(void)
{
	sender_close_calls++;
}
#endif

#define lb_add_payload_mm_authvar_presence_endpoint(header) \
	lb_add_payload_mm_authvar_presence_endpoint((header), active_table_end)

enum race_mode {
	RACE_NONE,
	RACE_POISON_LOSES,
	RACE_POISON_WINS,
	RACE_RESERVED_LOSER,
};

static const struct lb_authvar_presence_endpoint expected = {
	.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
	.size = sizeof(struct lb_authvar_presence_endpoint),
	.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
	.header_size = sizeof(struct lb_authvar_presence_endpoint),
	.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
	.generation = 0x12345678U,
	.communication_base = 0x100000,
	.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
	.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
	.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
	.trigger_width = 1,
	.trigger_address = 0xb2,
	.trigger_value = 0xe2,
	.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
	.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
};

bool platform_payload_mm_authvar_presence_required(void)
{
	required_calls++;
	if (reenter_required) {
		reenter_required = false;
		assert(payload_mm_authvar_presence_publication_reserve() == CB_ERR);
	}
	return required;
}

bool platform_payload_mm_authvar_presence_composition(
	struct payload_mm_authvar_presence_composition *composition)
{
	composition_calls++;
	memset(composition, 0xa5, sizeof(*composition));
	if (mutate_composition_table)
		active_header->table_bytes = 2;
	if (race_mode == RACE_POISON_LOSES || race_mode == RACE_POISON_WINS) {
		__atomic_store_n(&race_phase, 1, __ATOMIC_RELEASE);
		while (__atomic_load_n(&race_phase, __ATOMIC_ACQUIRE) != 2)
			sched_yield();
	}
	if (reenter_composition) {
		reenter_composition = false;
		assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
			CB_ERR);
	}
	return composition_ok;
}

enum cb_err payload_mm_authvar_presence_producer_reserve(void)
{
	reserve_calls++;
	return reserve_status;
}

enum cb_err payload_mm_authvar_presence_producer_compose(
	const struct payload_mm_authvar_presence_composition *composition)
{
	compose_calls++;
	assert(composition != NULL);
	if (reenter_compose) {
		reenter_compose = false;
		assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
			CB_ERR);
	}
	return compose_status;
}

enum cb_err payload_mm_authvar_presence_producer_publication_take(
	struct payload_mm_authvar_presence_receipt *receipt)
{
	take_calls++;
	take_sequence = ++sequence;
	if (race_mode == RACE_RESERVED_LOSER) {
		__atomic_store_n(&race_phase, 2, __ATOMIC_RELEASE);
		while (__atomic_load_n(&race_phase, __ATOMIC_ACQUIRE) != 3)
			sched_yield();
	}
	if (reenter_take) {
		reenter_take = false;
		assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
			CB_ERR);
	}
	if (take_status == CB_SUCCESS) {
		receipt->endpoint = expected;
		receipt->identity = (uintptr_t)receipt;
		receipt->nonce = 1;
		receipt->active = 1;
	}
	if (mutate_take_header)
		active_header->table_entries++;
	if (mutate_take_record_tail)
		*((uint8_t *)active_header + sizeof(*active_header) + 16) ^= 1;
	if (mutate_take_publication_state)
		payload_mm_authvar_presence_publication_reset_test();
	return take_status;
}

enum cb_err
payload_mm_authvar_presence_producer_publication_complete(struct payload_mm_authvar_presence_receipt *receipt)
{
	complete_calls++;
	assert(receipt->active == 1 && receipt->identity == (uintptr_t)receipt);
	if (complete_error)
		return CB_ERR;
	memset(receipt, 0, sizeof(*receipt));
	return CB_SUCCESS;
}

void
payload_mm_authvar_presence_producer_publication_fail_stop(struct payload_mm_authvar_presence_receipt *receipt)
{
	assert(receipt->active == 1 && receipt->identity == (uintptr_t)receipt);
	memset(receipt, 0, sizeof(*receipt));
	_exit(73);
}

void payload_mm_authvar_presence_producer_abort(void)
{
	abort_calls++;
}

struct lb_record *lb_new_record(struct lb_header *header)
{
	struct lb_record *record = (void *)((uint8_t *)header + sizeof(*header) +
		header->table_bytes);

	assert(header == active_header);
	record_calls++;
	record_sequence = ++sequence;
	if (header->table_entries) {
		assert(IS_ALIGNED(record->size, LB_ENTRY_ALIGN));
		header->table_bytes += record->size;
		record = (void *)((uint8_t *)header + sizeof(*header) +
			header->table_bytes);
	}
	header->table_entries++;
	record->tag = LB_TAG_UNUSED;
	record->size = sizeof(*record);
	return record;
}

void payload_mm_authvar_presence_publication_pre_poison_test_hook(
	uint32_t observed_state)
{
	if (race_mode != RACE_POISON_LOSES)
		return;
	assert(observed_state == 1);
	__atomic_store_n(&race_phase, 2, __ATOMIC_RELEASE);
	while (__atomic_load_n(&race_phase, __ATOMIC_ACQUIRE) != 3)
		sched_yield();
}

void payload_mm_authvar_presence_publication_pre_reserve_claim_test_hook(void)
{
	unsigned int phase;

	if (race_mode != RACE_RESERVED_LOSER)
		return;
	phase = 0;
	if (!__atomic_compare_exchange_n(&race_phase, &phase, 1, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return;
	while (__atomic_load_n(&race_phase, __ATOMIC_ACQUIRE) != 2)
		sched_yield();
}

static bool zero(const void *buffer, size_t size)
{
	const uint8_t *bytes = buffer;
	uint8_t combined = 0;

	while (size--)
		combined |= *bytes++;
	return combined == 0;
}

void payload_mm_authvar_presence_publication_scrub_test_hook(
	const void *buffer, size_t size)
{
	assert(zero(buffer, size));
	if (size == sizeof(struct payload_mm_authvar_presence_composition))
		scrubbed_compositions++;
	else if (size == sizeof(struct lb_authvar_presence_endpoint))
		scrubbed_endpoints++;
	else if (size ==
		 sizeof(struct payload_mm_authvar_presence_receipt))
		scrubbed_receipts++;
	else
		abort();
}

static void reset(void)
{
	payload_mm_authvar_presence_publication_reset_test();
	required = true;
	composition_ok = true;
	reserve_status = compose_status = take_status = CB_SUCCESS;
	reserve_calls = 0;
	compose_calls = 0;
	take_calls = 0;
	complete_calls = 0;
	abort_calls = 0;
	sender_close_calls = 0;
	record_calls = required_calls = composition_calls = 0;
	scrubbed_compositions = 0;
	scrubbed_endpoints = 0;
	scrubbed_receipts = 0;
	sequence = take_sequence = record_sequence = 0;
	reenter_required = reenter_composition = reenter_compose = false;
	reenter_take = false;
	mutate_composition_table = false;
	mutate_take_header = false;
	mutate_take_record_tail = false;
	mutate_take_publication_state = false;
	complete_error = false;
	race_mode = RACE_NONE;
	race_phase = 0;
	owner_status = contender_status = CB_ERR;
	memset(table_storage, 0xcc, sizeof(table_storage));
	active_header = (void *)table_storage;
	memset(active_header, 0, sizeof(*active_header));
	active_header->header_bytes = sizeof(*active_header);
	active_table_end = (uintptr_t)active_header + sizeof(*active_header) +
		sizeof(expected);
}

static void inert_default(void)
{
	reset();
	required = false;
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
		CB_SUCCESS);
	assert(required_calls == 1 && !reserve_calls && !composition_calls &&
		!compose_calls && !take_calls && !record_calls && !abort_calls);
}

static void success(void)
{
	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
		CB_SUCCESS);
	assert(reserve_calls == 1 && composition_calls == 1 && compose_calls == 1 &&
		take_calls == 1 && complete_calls == 1 && record_calls == 1 &&
		abort_calls == 0);
	assert(record_sequence && record_sequence < take_sequence);
	assert(!memcmp(table_storage + sizeof(*active_header), &expected,
		       sizeof(expected)));
	assert(scrubbed_compositions == 1 && scrubbed_endpoints == 0 &&
	       scrubbed_receipts == 1);
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(record_calls == 1 && take_calls == 1 && abort_calls == 0);
}

static void failures(void)
{
	u8 before[sizeof(table_storage)];

	reset();
	reserve_status = CB_ERR;
	assert(payload_mm_authvar_presence_publication_reserve() == CB_ERR);
	assert(reserve_calls == 1 && abort_calls == 1 && !record_calls);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	composition_ok = false;
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(abort_calls == 1 && !compose_calls && !take_calls && !record_calls);
	assert(scrubbed_compositions == 1 && scrubbed_endpoints == 0);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	compose_status = CB_ERR;
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(abort_calls == 1 && compose_calls == 1 && !take_calls && !record_calls);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	take_status = CB_ERR;
	memcpy(before, table_storage, sizeof(before));
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(!abort_calls && take_calls == 1 && record_calls == 1);
#if CONFIG(PAYLOAD_MM_AUTHVAR_PRESENCE_TUPLE_SENDER) && CONFIG(SMM_INVOCATION_RUNTIME_BINDING)
	assert(sender_close_calls == 1);
#endif
	assert(!memcmp(before, table_storage, sizeof(before)));

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(lb_add_payload_mm_authvar_presence_endpoint(NULL) == CB_ERR);
	assert(abort_calls == 1 && !composition_calls && !record_calls);
}

static void assert_capacity_rejected(void)
{
	u8 before[sizeof(table_storage)];

	memcpy(before, table_storage, sizeof(before));
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(abort_calls == 1 && !composition_calls && !compose_calls &&
	       !take_calls && !record_calls);
	assert(!memcmp(before, table_storage, sizeof(before)));
}

static void capacity_boundaries(void)
{
	struct lb_record *last;

	reset();
	active_table_end--;
	assert_capacity_rejected();

	reset();
	last = (void *)(table_storage + sizeof(*active_header));
	last->tag = 0xdead;
	last->size = 12;
	active_header->table_entries = 1;
	active_header->table_bytes = 0;
	active_table_end += last->size;
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
		CB_SUCCESS);
	assert(active_header->table_entries == 2);
	assert(active_header->table_bytes == last->size);
	assert(!memcmp(table_storage + sizeof(*active_header) + last->size,
		       &expected, sizeof(expected)));

	reset();
	last = (void *)(table_storage + sizeof(*active_header));
	last->size = 12;
	active_header->table_entries = 1;
	active_table_end += last->size - 1;
	assert_capacity_rejected();

	reset();
	active_header->table_bytes = 2;
	assert_capacity_rejected();

	reset();
	last = (void *)(table_storage + sizeof(*active_header));
	last->size = 7;
	active_header->table_entries = 1;
	assert_capacity_rejected();

	reset();
	last = (void *)(table_storage + sizeof(*active_header));
	last->size = 10;
	active_header->table_entries = 1;
	assert_capacity_rejected();

	reset();
	active_header->table_bytes = 4;
	last = (void *)(table_storage + sizeof(*active_header) + 4);
	last->size = UINT32_MAX - 3;
	active_header->table_entries = 1;
	active_table_end = UINTPTR_MAX;
	assert_capacity_rejected();

	reset();
	last = (void *)(table_storage + sizeof(*active_header));
	last->size = UINT32_MAX - 63;
	active_header->table_entries = 1;
	active_table_end = UINTPTR_MAX;
	assert_capacity_rejected();

	reset();
	active_header->table_entries = UINT32_MAX;
	assert_capacity_rejected();

	reset();
	active_table_end = (uintptr_t)active_header - 1;
	assert_capacity_rejected();

	reset();
	active_header = (void *)(table_storage + 1);
	active_table_end = (uintptr_t)table_storage + sizeof(table_storage);
	assert_capacity_rejected();

	reset();
	mutate_composition_table = true;
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(abort_calls == 1 && composition_calls == 1 && compose_calls == 1 &&
	       !take_calls && !record_calls);
}

static void committed_mutation_fail_stops(void)
{
	pid_t child;
	int status;

	for (unsigned int mutation = 0; mutation < 4; mutation++) {
		child = fork();
		assert(child >= 0);
		if (!child) {
			reset();
			mutate_take_header = mutation == 0;
			mutate_take_record_tail = mutation == 1;
			mutate_take_publication_state = mutation == 2;
			complete_error = mutation == 3;
			assert(payload_mm_authvar_presence_publication_reserve() ==
				CB_SUCCESS);
			(void)lb_add_payload_mm_authvar_presence_endpoint(active_header);
			_exit(0);
		}
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 73);
	}
}

static void hostile_reentry(void)
{
	reset();
	reenter_required = true;
	assert(payload_mm_authvar_presence_publication_reserve() == CB_ERR);
	assert(abort_calls >= 1 && !reserve_calls && !record_calls);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	reenter_composition = true;
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(abort_calls >= 1 && !take_calls && !record_calls);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	reenter_compose = true;
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) == CB_ERR);
	assert(abort_calls >= 1 && compose_calls == 1 && !take_calls &&
		!record_calls);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	reenter_take = true;
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
		CB_SUCCESS);
	assert(take_calls == 1 && record_calls == 1 && !abort_calls);
	assert(lb_add_payload_mm_authvar_presence_endpoint(active_header) ==
		CB_ERR);
	assert(record_calls == 1);
}

static void *owner_thread(void *unused)
{
	(void)unused;
	owner_status = lb_add_payload_mm_authvar_presence_endpoint(active_header);
	if (race_mode == RACE_POISON_LOSES)
		__atomic_store_n(&race_phase, 3, __ATOMIC_RELEASE);
	return NULL;
}

static void *contender_thread(void *unused)
{
	(void)unused;
	contender_status =
		lb_add_payload_mm_authvar_presence_endpoint(active_header);
	if (race_mode == RACE_POISON_WINS)
		__atomic_store_n(&race_phase, 2, __ATOMIC_RELEASE);
	else if (race_mode == RACE_RESERVED_LOSER)
		__atomic_store_n(&race_phase, 3, __ATOMIC_RELEASE);
	return NULL;
}

static void publication_races(void)
{
	pthread_t owner;
	pthread_t contender;

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	race_mode = RACE_POISON_LOSES;
	assert(!pthread_create(&owner, NULL, owner_thread, NULL));
	while (__atomic_load_n(&race_phase, __ATOMIC_ACQUIRE) != 1)
		sched_yield();
	assert(!pthread_create(&contender, NULL, contender_thread, NULL));
	assert(!pthread_join(owner, NULL));
	assert(!pthread_join(contender, NULL));
	assert(owner_status == CB_SUCCESS && contender_status == CB_ERR);
	assert(record_calls == 1 && take_calls == 1 && abort_calls == 0);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	race_mode = RACE_POISON_WINS;
	assert(!pthread_create(&owner, NULL, owner_thread, NULL));
	while (__atomic_load_n(&race_phase, __ATOMIC_ACQUIRE) != 1)
		sched_yield();
	assert(!pthread_create(&contender, NULL, contender_thread, NULL));
	assert(!pthread_join(owner, NULL));
	assert(!pthread_join(contender, NULL));
	assert(owner_status == CB_ERR && contender_status == CB_ERR);
	assert(record_calls == 0 && take_calls == 0 && abort_calls == 1);

	reset();
	assert(payload_mm_authvar_presence_publication_reserve() == CB_SUCCESS);
	race_mode = RACE_RESERVED_LOSER;
	assert(!pthread_create(&contender, NULL, contender_thread, NULL));
	while (__atomic_load_n(&race_phase, __ATOMIC_ACQUIRE) != 1)
		sched_yield();
	assert(!pthread_create(&owner, NULL, owner_thread, NULL));
	assert(!pthread_join(owner, NULL));
	assert(!pthread_join(contender, NULL));
	assert(owner_status == CB_SUCCESS && contender_status == CB_ERR);
	assert(record_calls == 1 && take_calls == 1 && abort_calls == 0);
}

int main(void)
{
	inert_default();
	success();
	failures();
	capacity_boundaries();
	committed_mutation_fail_stops();
	hostile_reentry();
	publication_races();
	return 0;
}
