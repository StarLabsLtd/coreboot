/* SPDX-License-Identifier: GPL-2.0-only */

#include <boot/payload_mm_authvar_presence_producer.h>
#include <bootmem.h>
#include <random.h>
#include <string.h>

enum producer_state {
	PRODUCER_EMPTY,
	PRODUCER_RESERVED,
	PRODUCER_BUSY,
	PRODUCER_READY,
	PRODUCER_PUBLISHED,
	PRODUCER_FAILED,
};

struct producer_storage {
	uint32_t state;
	struct bootmem_aligned_reservation_handle reservation;
	struct lb_authvar_presence_endpoint endpoint;
	uint64_t backing_base;
	uint32_t backing_size;
	struct payload_mm_authvar_presence_composition policy;
	uint8_t context[PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_CONTEXT_MAX];
	uint8_t sealed_context[PAYLOAD_MM_AUTHVAR_PRESENCE_PRODUCER_CONTEXT_MAX];
	bool authority_installed;
};

static struct producer_storage producer;

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
	if (__atomic_compare_exchange_n(&producer.state, &from, to, false,
		__ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		return true;
	if (from == PRODUCER_BUSY)
		(void)__atomic_compare_exchange_n(&producer.state, &from,
			PRODUCER_FAILED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	return false;
}

static bool busy(void)
{
	return __atomic_load_n(&producer.state, __ATOMIC_ACQUIRE) == PRODUCER_BUSY;
}

static bool context_unchanged(void)
{
	return !memcmp(producer.context, producer.sealed_context,
		producer.policy.context_size);
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

	if (!busy() || !producer.authority_installed || !context_unchanged() ||
	    !producer.policy.cold_boot(context) || !busy() || !context_unchanged())
		return false;
	if (!producer.policy.dma_protected(context, base, size) ||
	    !busy() || !context_unchanged())
		return false;
	proven |= LB_AUTHVAR_PRESENCE_DMA_PROTECTED;
	if (!producer.policy.cpu_rendezvous_ready(context) ||
	    !busy() || !context_unchanged())
		return false;
	proven |= LB_AUTHVAR_PRESENCE_CPU_RENDEZVOUS;
	if (!producer.policy.lifecycle_sealed(context) || !busy() ||
	    !context_unchanged())
		return false;
	proven |= LB_AUTHVAR_PRESENCE_LIFECYCLE_SEALED;
	if (!producer.policy.cold_reset_ready(context) || !busy() ||
	    !context_unchanged() || !producer.policy.platform_ready(context) ||
	    !busy() || !context_unchanged() ||
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

static void rollback(void)
{
	if (producer.authority_installed && producer.policy.authority_close)
		producer.policy.authority_close(producer.policy.context_size ?
			producer.sealed_context : NULL);
	if (producer.backing_base && producer.backing_size ==
		PAYLOAD_MM_AUTHVAR_PRESENCE_BACKING_SIZE)
		scrub((void *)(uintptr_t)producer.backing_base,
			producer.backing_size);
	clear_storage();
	__atomic_store_n(&producer.state, PRODUCER_FAILED, __ATOMIC_RELEASE);
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
		policy->authority_install && policy->authority_close &&
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
	uint64_t random[6];
	uint32_t flags;
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
	memcpy(seed.capability, &random[2], sizeof(seed.capability));
	if (payload_mm_authvar_presence_endpoint_validate(&seed.endpoint) != CB_SUCCESS)
		goto fail_local;

	sealed_seed = seed;
	/* A true return means the seed was copied into protected SMM and consumed. */
	producer.authority_installed = true;
	if (producer.policy.authority_install(policy_context(), &seed) != CB_SUCCESS ||
	    !busy() || !context_unchanged() ||
	    memcmp(&seed, &sealed_seed, sizeof(seed)))
		goto fail_local;
	if (!proofs(reservation.base, reservation.size, &flags))
		goto fail_local;

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
	if (!claim(PRODUCER_BUSY, PRODUCER_READY))
		goto fail_local;
	return CB_SUCCESS;

fail_local:
	scrub(&seed, sizeof(seed));
	scrub(&sealed_seed, sizeof(sealed_seed));
	scrub(&message, sizeof(message));
	scrub(random, sizeof(random));
fail:
	rollback();
	return CB_ERR;
}

enum cb_err payload_mm_authvar_presence_producer_publication_take(
	struct lb_authvar_presence_endpoint *record)
{
	struct lb_authvar_presence_endpoint endpoint;
	uint32_t flags;

	if (!record) {
		if (claim(PRODUCER_READY, PRODUCER_BUSY))
			rollback();
		return CB_ERR;
	}
	memset(record, 0, sizeof(*record));
	if (!claim(PRODUCER_READY, PRODUCER_BUSY))
		return CB_ERR;
	if (!proofs(producer.backing_base, producer.backing_size, &flags) ||
	    flags != producer.endpoint.flags ||
	    payload_mm_authvar_presence_endpoint_validate(&producer.endpoint) !=
		CB_SUCCESS) {
		rollback();
		return CB_ERR;
	}
	endpoint = producer.endpoint;
	if (!claim(PRODUCER_BUSY, PRODUCER_PUBLISHED)) {
		rollback();
		return CB_ERR;
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

		if (state == PRODUCER_PUBLISHED || state == PRODUCER_FAILED)
			return;
		if (state == PRODUCER_BUSY || state == PRODUCER_EMPTY) {
			if (__atomic_compare_exchange_n(&producer.state, &expected,
				PRODUCER_FAILED, false, __ATOMIC_ACQ_REL,
				__ATOMIC_ACQUIRE))
				return;
			continue;
		}
		if ((state == PRODUCER_RESERVED || state == PRODUCER_READY) &&
		    __atomic_compare_exchange_n(&producer.state, &expected,
			PRODUCER_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			rollback();
			return;
		}
		if (state != PRODUCER_RESERVED && state != PRODUCER_READY &&
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
}

const void *payload_mm_authvar_presence_producer_test_state(size_t *size)
{
	if (size)
		*size = sizeof(producer);
	return &producer;
}
#endif
