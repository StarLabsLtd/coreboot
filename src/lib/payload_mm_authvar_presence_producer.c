/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_producer.h>
#include <boot/payload_mm_authvar_presence_transaction.h>
#include <bootmem.h>
#include <random.h>
#include <string.h>

enum producer_state {
	PRODUCER_EMPTY,
	PRODUCER_RESERVED,
	PRODUCER_BUSY,
	PRODUCER_PREPARED,
	PRODUCER_PUBLISHED,
	PRODUCER_FAILED,
	PRODUCER_FINALIZING,
	PRODUCER_PREPARING,
	PRODUCER_ABORT_REQUESTED,
};

enum producer_backing_owner {
	PRODUCER_BACKING_NONE,
	PRODUCER_BACKING_RESERVATION,
	PRODUCER_BACKING_PRODUCER,
	PRODUCER_BACKING_AUTHORITY,
};

struct producer_storage {
	uint32_t state;
	struct bootmem_aligned_reservation_handle reservation;
	struct lb_authvar_presence_endpoint endpoint;
	uint64_t backing_base;
	uint32_t backing_size;
	uint32_t backing_owner;
	struct payload_mm_authvar_presence_composition policy;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_CONTEXT_MAX];
	uint8_t sealed_context[PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_CONTEXT_MAX];
	struct payload_mm_authvar_presence_transaction_binding transaction;
	struct payload_mm_authvar_presence_transaction_binding sealed_transaction;
};

static struct producer_storage producer;
#if ENV_TEST
static payload_mm_authvar_presence_producer_test_hook_fn before_prepare_hook;
#endif

_Static_assert(PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE >=
	PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
	"presence backing must contain the mailbox");
_Static_assert(!(PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE % 4096U) &&
	PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT >= 4096U,
	"presence backing must satisfy bootmem granularity");

static __noinline void scrub(void *buffer, size_t size)
{
	volatile uint8_t *bytes = buffer;

	while (size--)
		*bytes++ = 0;
	__asm__ __volatile__("" : : "r" (bytes) : "memory");
}

static bool claim(uint32_t from, uint32_t to)
{
	return __atomic_compare_exchange_n(&producer.state, &from, to, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static bool busy(void)
{
	return __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) == PRODUCER_BUSY;
}

static bool preparing(void)
{
	return __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) ==
		PRODUCER_PREPARING;
}

static bool active(void)
{
	const uint32_t state = __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE);

	return state == PRODUCER_BUSY || state == PRODUCER_PREPARING ||
		state == PRODUCER_ABORT_REQUESTED || state == PRODUCER_PREPARED ||
		state == PRODUCER_FINALIZING;
}

static bool context_unchanged(void)
{
	return !memcmp(producer.context, producer.sealed_context,
		producer.policy.context_size);
}

static bool transaction_unchanged(void)
{
	return !memcmp(&producer.transaction, &producer.sealed_transaction,
		sizeof(producer.transaction));
}

static void *policy_context(void)
{
	return producer.policy.context_size ? producer.context : NULL;
}

static bool proofs(uint64_t base, uint64_t size, uint32_t *flags)
{
	uint32_t proven = LB_AUTHVAR_PRESENCE_COREBOOT_SMM_OWNER |
		LB_AUTHVAR_PRESENCE_FIXED_COMMUNICATION |
		LB_AUTHVAR_PRESENCE_ONE_SHOT_CAPABILITY;
	void *context = policy_context();

	if (!active() || !transaction_unchanged() || !context_unchanged() ||
	    !producer.policy.cold_boot(context) || !active() ||
	    !context_unchanged() || !transaction_unchanged())
		return false;
	if (!producer.policy.dma_protected(context, base, size) ||
	    !active() || !context_unchanged() || !transaction_unchanged())
		return false;
	proven |= LB_AUTHVAR_PRESENCE_DMA_PROTECTED;
	if (!producer.policy.cpu_rendezvous_ready(context) ||
	    !active() || !context_unchanged() || !transaction_unchanged())
		return false;
	proven |= LB_AUTHVAR_PRESENCE_CPU_RENDEZVOUS;
	if (!producer.policy.lifecycle_sealed(context) || !active() ||
	    !context_unchanged() || !transaction_unchanged())
		return false;
	proven |= LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED;
	if (!producer.policy.cold_reset_ready(context) || !active() ||
	    !context_unchanged() || !transaction_unchanged() ||
	    !producer.policy.platform_ready(context) || !active() ||
	    !context_unchanged() || !transaction_unchanged() ||
	    proven != LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS)
		return false;
	*flags = proven;
	return true;
}

static void clear_storage(void)
{
	scrub((uint8_t *)&producer + sizeof(producer.state),
		sizeof(producer) - sizeof(producer.state));
}

static __noreturn void fail_stop(void);

static void rollback(void)
{
	struct bootmem_aligned_reservation reservation;

	if (producer.backing_owner == PRODUCER_BACKING_AUTHORITY &&
	    producer.policy.authority_abort && producer.sealed_transaction.generation &&
	    producer.sealed_transaction.transaction_id &&
	    (__atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) ==
		PRODUCER_BUSY ||
	     __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) ==
		PRODUCER_PREPARING ||
	     __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) ==
		PRODUCER_ABORT_REQUESTED ||
	     __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) ==
		PRODUCER_PREPARED ||
	     __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) ==
		PRODUCER_FINALIZING)) {
		struct payload_mm_authvar_presence_transaction_ack ack;
		struct payload_mm_authvar_presence_transaction_binding work =
			producer.sealed_transaction;
		uint64_t saved_rax = UINT64_MAX;

		memset(&ack, 0xa5, sizeof(ack));
		if (producer.policy.authority_abort(policy_context(),
			&work, &ack, &saved_rax) != CB_SUCCESS ||
		    memcmp(&work, &producer.sealed_transaction, sizeof(work)) ||
		    !payload_mm_authvar_presence_transaction_ack_valid(
			&producer.sealed_transaction,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT, &ack,
			saved_rax))
			fail_stop();
		scrub(&ack, sizeof(ack));
		scrub(&work, sizeof(work));
	}
	if (producer.backing_owner == PRODUCER_BACKING_PRODUCER &&
	    producer.backing_base && producer.backing_size ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE)
		scrub((void *)(uintptr_t)producer.backing_base,
			producer.backing_size);
	if (producer.backing_owner == PRODUCER_BACKING_RESERVATION &&
	    !bootmem_aligned_reservation_query(&producer.reservation,
		&reservation) &&
	    reservation.base &&
	    reservation.size == PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE &&
	    reservation.tag == BM_MEM_RESERVED && !reservation.reserved &&
	    reservation.base <= UINTPTR_MAX - reservation.size &&
	    !(reservation.base % PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT))
		scrub((void *)(uintptr_t)reservation.base, reservation.size);
	clear_storage();
	__atomic_store_n(&producer.state, PRODUCER_FAILED, __ATOMIC_RELEASE);
}

static __noreturn void fail_stop(void)
{
	payload_mm_authvar_presence_producer_fail_stop_fn callback =
		producer.policy.fail_stop;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_CONTEXT_MAX];
	const size_t context_size = producer.policy.context_size;

	if (context_size)
		memcpy(context, producer.context, context_size);
	if (producer.backing_owner == PRODUCER_BACKING_PRODUCER &&
	    producer.backing_base && producer.backing_size ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE)
		scrub((void *)(uintptr_t)producer.backing_base,
			producer.backing_size);
	clear_storage();
	__atomic_store_n(&producer.state, PRODUCER_FAILED, __ATOMIC_RELEASE);
	callback(context_size ? context : NULL);
	__builtin_unreachable();
}

enum cb_err payload_mm_authvar_presence_producer_reserve(void)
{
	const struct bootmem_aligned_reservation_request request = {
		.revision = BOOTMEM_ALIGNED_RESERVATION_REVISION,
		.size = sizeof(request),
		.bytes = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE,
		.alignment = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT,
		.limit_exclusive = 1ULL << 32,
		.tag = BM_MEM_RESERVED,
	};

	if (!claim(PRODUCER_EMPTY, PRODUCER_BUSY))
		return CB_ERR;
	if (bootmem_aligned_reservation_register(&request,
		&producer.reservation)) {
		rollback();
		return CB_ERR;
	}
	producer.backing_owner = PRODUCER_BACKING_RESERVATION;
	if (!claim(PRODUCER_BUSY, PRODUCER_RESERVED)) {
		rollback();
		return CB_ERR;
	}
	return CB_SUCCESS;
}

static bool policy_valid(
	const struct payload_mm_authvar_presence_composition *policy)
{
	return policy && policy->revision ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_REVISION &&
		policy->size == sizeof(*policy) && policy->trigger_address <= UINT16_MAX &&
		policy->trigger_value <= UINT8_MAX && policy->cold_boot &&
		policy->authority_prepare && policy->authority_commit &&
		policy->authority_abort && policy->fail_stop &&
		policy->transaction_maximum_cpus &&
		policy->transaction_initiator_cpu <
			policy->transaction_maximum_cpus &&
		policy->dma_protected && policy->cpu_rendezvous_ready &&
		policy->cold_reset_ready && policy->lifecycle_sealed &&
		policy->platform_ready &&
		policy->context_size <= PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_CONTEXT_MAX &&
		((policy->context_size == 0 && !policy->context) ||
		 (policy->context_size != 0 && policy->context));
}

enum cb_err payload_mm_authvar_presence_producer_compose(
	const struct payload_mm_authvar_presence_composition *composition)
{
	struct bootmem_aligned_reservation reservation;
	struct payload_mm_authvar_presence_seed seed;
	struct payload_mm_authvar_presence_seed sealed_seed;
	struct payload_mm_authvar_presence_message message;
	struct payload_mm_authvar_presence_transaction_ack transaction_ack;
	struct payload_mm_authvar_presence_transaction_binding transaction_work;
	struct payload_mm_authvar_presence_transaction_binding abort_work;
	uint64_t random[6];
	uint64_t transaction_random[6] = { 0 };
	uint64_t transaction_rax = UINT64_MAX;
	uint32_t flags;
	bool abort_confirmed;
	size_t i;

	if (!claim(PRODUCER_RESERVED, PRODUCER_BUSY))
		return CB_ERR;
	if (!policy_valid(composition))
		goto fail;
	producer.policy = *composition;
	if (composition->context_size) {
		memcpy(producer.context, composition->context,
			composition->context_size);
		memcpy(producer.sealed_context, composition->context,
			composition->context_size);
	}
	producer.policy.context = policy_context();
	if (!producer.policy.cold_boot(policy_context()) || !busy() ||
	    !context_unchanged() ||
	    bootmem_aligned_reservation_query(&producer.reservation, &reservation) ||
	    reservation.size != PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE ||
	    reservation.tag != BM_MEM_RESERVED || reservation.reserved ||
	    !reservation.base || reservation.base > UINTPTR_MAX - reservation.size ||
	    reservation.base + reservation.size > (1ULL << 32) ||
	    reservation.base % PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_ALIGNMENT ||
	    reservation.base + reservation.size < reservation.base)
		goto fail;
	producer.backing_base = reservation.base;
	producer.backing_size = (uint32_t)reservation.size;
	producer.backing_owner = PRODUCER_BACKING_PRODUCER;
	scrub((void *)(uintptr_t)reservation.base, reservation.size);

	memset(&seed, 0, sizeof(seed));
	memset(&message, 0, sizeof(message));
	for (i = 0; i < ARRAY_SIZE(random); i++)
		if (get_random_number_64(&random[i]) != CB_SUCCESS || !busy())
			goto fail_local;
	if (!random[0] || !random[1] || !(random[2] | random[3] | random[4] | random[5]))
		goto fail_local;

	seed.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_SEED_REVISION;
	seed.size = sizeof(seed);
	seed.endpoint = (struct lb_authvar_presence_endpoint) {
		.tag = LB_TAG_AUTHVAR_PRESENCE_ENDPOINT,
		.size = sizeof(seed.endpoint),
		.revision = LB_AUTHVAR_PRESENCE_ENDPOINT_REVISION,
		.header_size = sizeof(seed.endpoint),
		.flags = LB_AUTHVAR_PRESENCE_REQUIRED_FLAGS,
		.generation = random[0],
		.communication_base = reservation.base,
		.communication_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.message_size = PAYLOAD_MM_AUTHVAR_PRESENCE_MESSAGE_SIZE,
		.transport = LB_AUTHVAR_PRESENCE_TRANSPORT_APM_IO8,
		.trigger_width = 1,
		.trigger_address = producer.policy.trigger_address,
		.trigger_value = producer.policy.trigger_value,
		.action_scope = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE,
		.capability_size = LB_AUTHVAR_PRESENCE_CAPABILITY_SIZE,
	};
	seed.backing = (struct payload_mm_authvar_presence_backing) {
		.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_REVISION,
		.size = sizeof(seed.backing),
		.base = reservation.base,
		.bytes = reservation.size,
		.generation = seed.endpoint.generation,
		.tag = reservation.tag,
	};
	memcpy(seed.capability, &random[2], sizeof(seed.capability));
	if (payload_mm_authvar_presence_endpoint_validate(&seed.endpoint) != CB_SUCCESS)
		goto fail_local;

	sealed_seed = seed;
	if (!proofs(reservation.base, reservation.size, &flags))
		goto fail_local;
	for (i = 0; i < ARRAY_SIZE(transaction_random); i++)
		if (get_random_number_64(&transaction_random[i]) != CB_SUCCESS ||
		    !busy())
			goto fail_local;
	if (!transaction_random[0] || !transaction_random[1] ||
	    !(transaction_random[2] | transaction_random[3] |
	      transaction_random[4] | transaction_random[5]))
		goto fail_local;
	producer.transaction =
		(struct payload_mm_authvar_presence_transaction_binding) {
			.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_REVISION,
			.size = sizeof(producer.transaction),
			.generation = seed.endpoint.generation,
			.transaction_id = transaction_random[0],
			.nonce = transaction_random[1],
			.initiator_cpu = producer.policy.transaction_initiator_cpu,
			.maximum_cpus = producer.policy.transaction_maximum_cpus,
		};
	memcpy(producer.transaction.capability, &transaction_random[2],
		sizeof(producer.transaction.capability));
	producer.sealed_transaction = producer.transaction;
#if ENV_TEST
	if (before_prepare_hook)
		before_prepare_hook();
#endif
	if (!claim(PRODUCER_BUSY, PRODUCER_PREPARING))
		goto fail_local;
	transaction_work = producer.sealed_transaction;
	memset(&transaction_ack, 0xa5, sizeof(transaction_ack));
	if (producer.policy.authority_prepare(policy_context(), &seed,
		&transaction_work, &transaction_ack,
		&transaction_rax) != CB_SUCCESS ||
	    memcmp(&transaction_work, &producer.sealed_transaction,
		sizeof(transaction_work)) || !preparing() ||
	    !context_unchanged() || !transaction_unchanged() ||
	    memcmp(&seed, &sealed_seed, sizeof(seed)) ||
	    !payload_mm_authvar_presence_transaction_ack_valid(
		&producer.sealed_transaction,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_PREPARE,
		&transaction_ack, transaction_rax)) {
		abort_confirmed =
			payload_mm_authvar_presence_transaction_ack_valid(
			&producer.sealed_transaction,
			PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT,
			&transaction_ack, transaction_rax);
		if (!abort_confirmed) {
			abort_work = producer.sealed_transaction;
			memset(&transaction_ack, 0xa5, sizeof(transaction_ack));
			transaction_rax = UINT64_MAX;
			if (producer.policy.authority_abort(policy_context(),
				&abort_work, &transaction_ack,
				&transaction_rax) != CB_SUCCESS ||
			    memcmp(&abort_work, &producer.sealed_transaction,
				sizeof(abort_work)) ||
			    !payload_mm_authvar_presence_transaction_ack_valid(
				&producer.sealed_transaction,
				PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_ABORT,
				&transaction_ack, transaction_rax))
				{
					producer.backing_owner =
						PRODUCER_BACKING_AUTHORITY;
					fail_stop();
				}
		}
		producer.backing_owner = PRODUCER_BACKING_NONE;
		scrub(&transaction_ack, sizeof(transaction_ack));
		scrub(&transaction_work, sizeof(transaction_work));
		scrub(&abort_work, sizeof(abort_work));
		goto fail_aborted;
	}
	producer.backing_owner = PRODUCER_BACKING_AUTHORITY;
	scrub(&transaction_ack, sizeof(transaction_ack));
	scrub(&transaction_work, sizeof(transaction_work));
	scrub(&abort_work, sizeof(abort_work));
	scrub(transaction_random, sizeof(transaction_random));

	message.revision = PAYLOAD_MM_AUTHVAR_PRESENCE_REVISION;
	message.size = sizeof(message);
	message.action = LB_AUTHVAR_PRESENCE_ENTER_SETUP_MODE;
	message.generation = random[0];
	message.request_id = random[1];
	memcpy(message.capability, seed.capability, sizeof(message.capability));
	message.status = PAYLOAD_MM_AUTHVAR_PRESENCE_STATUS_PENDING;
	message.completion = PAYLOAD_MM_AUTHVAR_PRESENCE_PENDING;
	memcpy((void *)(uintptr_t)reservation.base, &message, sizeof(message));
	__atomic_thread_fence(__ATOMIC_RELEASE);
	if (!proofs(reservation.base, reservation.size, &flags))
		goto fail_local;
	seed.endpoint.flags = flags;
	producer.endpoint = seed.endpoint;
	scrub(&seed, sizeof(seed));
	scrub(&sealed_seed, sizeof(sealed_seed));
	scrub(&message, sizeof(message));
	scrub(random, sizeof(random));
	scrub(transaction_random, sizeof(transaction_random));
	if (!claim(PRODUCER_PREPARING, PRODUCER_PREPARED))
		goto fail_local;
	return CB_SUCCESS;

fail_aborted:
	scrub(&seed, sizeof(seed));
	scrub(&sealed_seed, sizeof(sealed_seed));
	scrub(&message, sizeof(message));
	scrub(random, sizeof(random));
	scrub(transaction_random, sizeof(transaction_random));
	if (producer.backing_owner == PRODUCER_BACKING_PRODUCER &&
	    producer.backing_base && producer.backing_size ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE)
		scrub((void *)(uintptr_t)producer.backing_base,
			producer.backing_size);
	clear_storage();
	__atomic_store_n(&producer.state, PRODUCER_FAILED, __ATOMIC_RELEASE);
	return CB_ERR;

fail_local:
	scrub(&seed, sizeof(seed));
	scrub(&sealed_seed, sizeof(sealed_seed));
	scrub(&message, sizeof(message));
	scrub(random, sizeof(random));
	scrub(transaction_random, sizeof(transaction_random));
	scrub(&transaction_ack, sizeof(transaction_ack));
	scrub(&transaction_work, sizeof(transaction_work));
	scrub(&abort_work, sizeof(abort_work));
fail:
	rollback();
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_producer_publication_take(
	struct lb_authvar_presence_endpoint *record)
{
	struct lb_authvar_presence_endpoint endpoint;
	struct payload_mm_authvar_presence_transaction_ack ack;
	struct payload_mm_authvar_presence_transaction_binding work;
	uint64_t saved_rax = UINT64_MAX;
	uint32_t flags;
	if (!record) {
		if (claim(PRODUCER_PREPARED, PRODUCER_FINALIZING))
			rollback();
		return CB_ERR;
	}
	memset(record, 0, sizeof(*record));
	if (!claim(PRODUCER_PREPARED, PRODUCER_FINALIZING))
		return CB_ERR;
	if (!proofs(producer.backing_base, producer.backing_size, &flags) ||
	    flags != producer.endpoint.flags ||
	    payload_mm_authvar_presence_endpoint_validate(&producer.endpoint) !=
		CB_SUCCESS) {
		rollback();
		return CB_ERR;
	}
	endpoint = producer.endpoint;
	work = producer.sealed_transaction;
	memset(&ack, 0xa5, sizeof(ack));
	if (producer.policy.authority_commit(policy_context(),
		&work, &ack, &saved_rax) != CB_SUCCESS ||
	    memcmp(&work, &producer.sealed_transaction, sizeof(work)) ||
	    !active() || !context_unchanged() || !transaction_unchanged() ||
	    !payload_mm_authvar_presence_transaction_ack_valid(
		&producer.sealed_transaction,
		PAYLOAD_MM_AUTHVAR_PRESENCE_TRANSACTION_COMMIT, &ack,
		saved_rax)) {
		scrub(&ack, sizeof(ack));
		scrub(&work, sizeof(work));
		fail_stop();
	}
	scrub(&ack, sizeof(ack));
	scrub(&work, sizeof(work));
	if (!claim(PRODUCER_FINALIZING, PRODUCER_PUBLISHED)) {
		fail_stop();
	}
	clear_storage();
	*record = endpoint;
	scrub(&endpoint, sizeof(endpoint));
	return CB_SUCCESS;
}

void payload_mm_authvar_presence_producer_abort(void)
{
	for (;;) {
		uint32_t state = __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE);
		uint32_t expected = state;

		if (state == PRODUCER_FINALIZING || state == PRODUCER_PUBLISHED ||
		    state == PRODUCER_FAILED || state == PRODUCER_ABORT_REQUESTED)
			return;
		if (state == PRODUCER_BUSY) {
			if (__atomic_compare_exchange_n(&producer.state, &expected,
				PRODUCER_ABORT_REQUESTED, false, __ATOMIC_ACQ_REL,
				__ATOMIC_ACQUIRE))
				return;
			continue;
		}
		if (state == PRODUCER_EMPTY) {
			if (__atomic_compare_exchange_n(&producer.state, &expected,
				PRODUCER_FAILED, false, __ATOMIC_ACQ_REL,
				__ATOMIC_ACQUIRE))
				return;
			continue;
		}
		if (state == PRODUCER_PREPARING &&
		    __atomic_compare_exchange_n(&producer.state, &expected,
			PRODUCER_ABORT_REQUESTED, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			return;
		if ((state == PRODUCER_RESERVED || state == PRODUCER_PREPARED) &&
		    __atomic_compare_exchange_n(&producer.state, &expected,
			PRODUCER_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			rollback();
			return;
		}
		if (state != PRODUCER_RESERVED && state != PRODUCER_PREPARED &&
		    state != PRODUCER_PREPARING && state != PRODUCER_ABORT_REQUESTED &&
		    state != PRODUCER_FINALIZING &&
		    __atomic_compare_exchange_n(&producer.state, &expected,
			PRODUCER_FAILED, false, __ATOMIC_ACQ_REL,
			__ATOMIC_ACQUIRE))
			return;
	}
}

#if ENV_TEST
void payload_mm_authvar_presence_producer_reset_test(void)
{
	scrub(&producer, sizeof(producer));
	before_prepare_hook = NULL;
}

const void *payload_mm_authvar_presence_producer_test_state(size_t *size)
{
	if (size)
		*size = sizeof(producer);
	return &producer;
}

void payload_mm_authvar_presence_producer_before_prepare_test_hook(
	payload_mm_authvar_presence_producer_test_hook_fn hook)
{
	before_prepare_hook = hook;
}
#endif
